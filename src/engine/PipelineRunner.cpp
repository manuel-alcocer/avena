#include "engine/PipelineRunner.h"

#include "graph/Connection.h"
#include "graph/Graph.h"
#include "graph/Node.h"
#include "media/ElementProperties.h"
#include "media/MediaInfo.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <functional>

#ifdef AVENA_HAVE_GSTREAMER
#include <gst/gst.h>
#endif

namespace ab {

bool PipelineRunner::available()
{
#ifdef AVENA_HAVE_GSTREAMER
    return true;
#else
    return false;
#endif
}

#ifdef AVENA_HAVE_GSTREAMER

namespace {

void ensureGstInit()
{
    static bool done = false;
    if (!done) { gst_init(nullptr, nullptr); done = true; }
}

MediaType mediaTypeFromCapsName(const QString& name)
{
    if (name.startsWith(QLatin1String("video/")) || name.startsWith(QLatin1String("image/")))
        return MediaType::Video;
    if (name.startsWith(QLatin1String("audio/")))
        return MediaType::Audio;
    if (name.startsWith(QLatin1String("text/")) || name.startsWith(QLatin1String("subtitle/")))
        return MediaType::Subtitle;
    return MediaType::Any;
}

GstElement* makeAdd(GstElement* pipeline, const char* factory)
{
    GstElement* e = gst_element_factory_make(factory, nullptr);
    if (e)
        gst_bin_add(GST_BIN(pipeline), e);
    return e;
}

/// Most muxers (mp4mux especially) require parsed, framed input. Picks a parser
/// for the encoded caps about to enter a muxer, or "" when none is needed.
const char* parserForCaps(GstCaps* caps)
{
    if (!caps || gst_caps_is_any(caps) || gst_caps_get_size(caps) == 0)
        return "";
    const GstStructure* s = gst_caps_get_structure(caps, 0);
    const QString name = QString::fromUtf8(gst_structure_get_name(s));

    if (name == QLatin1String("video/x-h264")) return "h264parse";
    if (name == QLatin1String("video/x-h265")) return "h265parse";
    if (name == QLatin1String("video/x-av1"))  return "av1parse";
    if (name == QLatin1String("video/x-vp9"))  return "vp9parse";
    if (name == QLatin1String("audio/x-opus")) return "opusparse";
    if (name == QLatin1String("audio/x-ac3"))  return "ac3parse";
    if (name == QLatin1String("audio/x-flac")) return "flacparse";
    if (name == QLatin1String("audio/mpeg")) {
        int mpegversion = 0;
        if (gst_structure_get_int(s, "mpegversion", &mpegversion) && mpegversion == 1)
            return "mpegaudioparse";
        return "aacparse";
    }
    return "";
}

GstPad* requestMuxerSink(GstElement* mux, MediaType type)
{
    const char* candidates[3] = {nullptr, nullptr, nullptr};
    int n = 0;
    switch (type) {
    case MediaType::Video:    candidates[n++] = "video_%u"; break;
    case MediaType::Audio:    candidates[n++] = "audio_%u"; break;
    case MediaType::Subtitle: candidates[n++] = "subtitle_%u"; break;
    default: break;
    }
    candidates[n++] = "sink_%u";
    for (int i = 0; i < n; ++i) {
        if (GstPad* pad = gst_element_request_pad_simple(mux, candidates[i]))
            return pad;
    }
    return nullptr;
}

/// Links a source pad into `dst`, inserting raw converters when needed and using
/// a request pad for muxers. `dynamic` syncs newly added converters when this is
/// called from decodebin's pad-added (pipeline already running).
bool linkPadToDst(GstElement* pipeline, GstPad* srcpad, MediaType type,
                  bool needConvert, GstElement* dst, bool dstIsMuxer, bool dynamic)
{
    GstPad* tail = static_cast<GstPad*>(gst_object_ref(srcpad));

    auto fail = [&] { gst_object_unref(tail); return false; };

    // Decouple every branch with a queue (prevents muxing/preroll deadlocks).
    {
        GstElement* queue = makeAdd(pipeline, "queue");
        if (!queue) return fail();
        if (dynamic) gst_element_sync_state_with_parent(queue);
        GstPad* sink = gst_element_get_static_pad(queue, "sink");
        const bool ok = gst_pad_link(tail, sink) == GST_PAD_LINK_OK;
        gst_object_unref(sink);
        if (!ok) return fail();
        gst_object_unref(tail);
        tail = gst_element_get_static_pad(queue, "src");
    }

    if (needConvert && type == MediaType::Video) {
        GstElement* vc = makeAdd(pipeline, "videoconvert");
        if (!vc) return fail();
        if (dynamic) gst_element_sync_state_with_parent(vc);
        GstPad* sink = gst_element_get_static_pad(vc, "sink");
        const bool ok = gst_pad_link(tail, sink) == GST_PAD_LINK_OK;
        gst_object_unref(sink);
        if (!ok) return fail();
        gst_object_unref(tail);
        tail = gst_element_get_static_pad(vc, "src");
    } else if (needConvert && type == MediaType::Audio) {
        GstElement* ac = makeAdd(pipeline, "audioconvert");
        GstElement* ar = makeAdd(pipeline, "audioresample");
        if (!ac || !ar) return fail();
        if (dynamic) { gst_element_sync_state_with_parent(ac); gst_element_sync_state_with_parent(ar); }
        gst_element_link(ac, ar);
        GstPad* sink = gst_element_get_static_pad(ac, "sink");
        const bool ok = gst_pad_link(tail, sink) == GST_PAD_LINK_OK;
        gst_object_unref(sink);
        if (!ok) return fail();
        gst_object_unref(tail);
        tail = gst_element_get_static_pad(ar, "src");
    }

    // Muxers need framed/parsed input — insert the right parser if available.
    if (dstIsMuxer) {
        GstCaps* caps = gst_pad_query_caps(tail, nullptr);
        const char* parserName = parserForCaps(caps);
        if (caps) gst_caps_unref(caps);
        if (parserName && parserName[0]) {
            if (GstElement* parser = makeAdd(pipeline, parserName)) {
                if (dynamic) gst_element_sync_state_with_parent(parser);
                GstPad* sink = gst_element_get_static_pad(parser, "sink");
                const bool ok = gst_pad_link(tail, sink) == GST_PAD_LINK_OK;
                gst_object_unref(sink);
                if (!ok) return fail();
                gst_object_unref(tail);
                tail = gst_element_get_static_pad(parser, "src");
            }
        }
    }

    GstPad* sinkpad = dstIsMuxer ? requestMuxerSink(dst, type)
                                 : gst_element_get_static_pad(dst, "sink");
    if (!sinkpad) return fail();

    const GstPadLinkReturn r = gst_pad_link(tail, sinkpad);
    gst_object_unref(sinkpad);
    gst_object_unref(tail);
    return r == GST_PAD_LINK_OK;
}

} // namespace

struct SourceContext {
    GstElement* pipeline = nullptr;
    GstElement* filesrc = nullptr;
    GstElement* decodebin = nullptr;
    gint64      trimStart = -1;   // ns, -1 = no start trim
    gint64      trimStop = -1;    // ns, -1 = no stop trim
    gint        trimEosSent = 0;  // atomic flag: EOS already sent to the source
    struct Pending {
        MediaType   type;
        GstElement* dst;
        bool        dstIsMuxer;
        bool        needConvert;
        bool        used = false;
    };
    std::vector<Pending> pending;
};

/// Buffer probe on a decoded (raw) pad: drops frames before `start`, and once
/// `stop` is reached sends EOS to the source so the whole pipeline drains
/// cleanly (no seek → muxer output stays valid; the encoder makes the first
/// kept frame a keyframe).
GstPadProbeReturn trimProbe(GstPad*, GstPadProbeInfo* info, gpointer user)
{
    auto* ctx = static_cast<SourceContext*>(user);
    auto* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buffer)
        return GST_PAD_PROBE_PASS;

    const GstClockTime pts = GST_BUFFER_PTS(buffer);
    if (!GST_CLOCK_TIME_IS_VALID(pts))
        return GST_PAD_PROBE_PASS;

    if (ctx->trimStop >= 0 && static_cast<gint64>(pts) >= ctx->trimStop) {
        if (ctx->filesrc &&
            g_atomic_int_compare_and_exchange(&ctx->trimEosSent, 0, 1))
            gst_element_send_event(ctx->filesrc, gst_event_new_eos());
        return GST_PAD_PROBE_DROP;
    }
    if (ctx->trimStart > 0 && static_cast<gint64>(pts) < ctx->trimStart)
        return GST_PAD_PROBE_DROP;
    return GST_PAD_PROBE_PASS;
}

struct PipelineRunner::Private {
    GstElement* pipeline = nullptr;
    QTimer      pollTimer;
    bool        running = false;
    std::vector<std::unique_ptr<SourceContext>> sources;
};

namespace {

void onPadAdded(GstElement* /*decodebin*/, GstPad* pad, gpointer user)
{
    auto* ctx = static_cast<SourceContext*>(user);

    MediaType type = MediaType::Any;
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps)
        caps = gst_pad_query_caps(pad, nullptr);
    if (caps) {
        if (gst_caps_get_size(caps) > 0) {
            const GstStructure* s = gst_caps_get_structure(caps, 0);
            type = mediaTypeFromCapsName(QString::fromUtf8(gst_structure_get_name(s)));
        }
        gst_caps_unref(caps);
    }

    for (auto& p : ctx->pending) {
        if (!p.used && p.type == type) {
            p.used = true;
            linkPadToDst(ctx->pipeline, pad, type, p.needConvert,
                         p.dst, p.dstIsMuxer, /*dynamic=*/true);

            // Attach the time-range trimming probe on the raw output pad.
            if (ctx->trimStart >= 0 || ctx->trimStop >= 0)
                gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, trimProbe, ctx, nullptr);
            return;
        }
    }
    // No consumer for this stream — leave it unlinked (it will be ignored).
}

QString variantToArg(const QVariant& v)
{
    if (v.typeId() == QMetaType::Bool)
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return v.toString();
}

void applyProperties(GstElement* element, const Node& node)
{
    for (auto it = node.properties.constBegin(); it != node.properties.constEnd(); ++it) {
        if (it.key() == QLatin1String("location") && it.value().toString().isEmpty())
            continue;
        // Skip our own synthetic properties that aren't GObject properties.
        static const QSet<QString> synthetic = {
            QStringLiteral("scale"), QStringLiteral("bypass"),
            QStringLiteral("start"), QStringLiteral("end"),
            QStringLiteral("name"), QStringLiteral("directory"),
            QStringLiteral("extension")};
        if (synthetic.contains(it.key()) || it.key().startsWith(QLatin1String("enable:")))
            continue;
        gst_util_set_object_arg(G_OBJECT(element),
                                it.key().toUtf8().constData(),
                                variantToArg(it.value()).toUtf8().constData());
    }
}

bool isToolNode(const Node& node)
{
    return node.typeId.startsWith(QLatin1String("tool."));
}

/// Parses "H:MM:SS(.mmm)" or a plain seconds value into nanoseconds; -1 if empty.
gint64 parseTimeNs(const QString& text)
{
    const QString s = text.trimmed();
    if (s.isEmpty())
        return -1;
    double seconds = 0.0;
    if (s.contains(QLatin1Char(':'))) {
        const QStringList parts = s.split(QLatin1Char(':'));
        for (const QString& p : parts)
            seconds = seconds * 60.0 + p.toDouble();
    } else {
        seconds = s.toDouble();
    }
    if (seconds < 0.0)
        return -1;
    return static_cast<gint64>(seconds * GST_SECOND);
}

/// Reads one Inspector output by tracing its media input back to the File
/// Source: file-level outputs (filename/directory/extension) are strings,
/// stream-level (width/height/fps/bitrate/channels/samplerate) are numbers.
/// Returns an invalid QVariant if unavailable.
QVariant inspectorValue(const Graph& graph, const Node& inspector,
                        const QString& outputName)
{
    // Connection feeding the inspector's media input (index 0).
    const Connection* incoming = nullptr;
    for (const auto& c : graph.connections()) {
        if (c->to.nodeId == inspector.id && c->to.index == 0) { incoming = c.get(); break; }
    }
    if (!incoming)
        return {};

    MediaType type = MediaType::Any;
    if (const Node* up = graph.node(incoming->from.nodeId))
        if (const Port* p = up->port(PortDirection::Output, incoming->from.index))
            type = p->mediaType;

    // Follow first-input chains upstream to the File Source.
    int nodeId = incoming->from.nodeId;
    for (int guard = 0; guard < 64; ++guard) {
        const Node* n = graph.node(nodeId);
        if (!n)
            return {};
        if (n->typeId == QLatin1String("source.file")) {
            const QString loc = n->properties.value(QStringLiteral("location")).toString();
            if (loc.isEmpty())
                return {};

            // File-level (string) outputs.
            const QFileInfo fi(loc);
            if (outputName == QLatin1String("filename"))  return fi.completeBaseName();
            if (outputName == QLatin1String("directory")) return fi.absolutePath();
            if (outputName == QLatin1String("extension")) return fi.suffix();

            // Stream-level (numeric) outputs.
            const MediaInfo info = MediaProbe::probe(loc);
            const MediaStreamInfo* s = nullptr;
            for (const MediaStreamInfo& st : info.streams) {
                if ((type == MediaType::Audio && st.kind == MediaStreamInfo::Kind::Audio) ||
                    (type != MediaType::Audio && st.kind == MediaStreamInfo::Kind::Video)) {
                    s = &st; break;
                }
            }
            if (!s)
                return {};
            if (outputName == QLatin1String("width"))      return s->width;
            if (outputName == QLatin1String("height"))     return s->height;
            if (outputName == QLatin1String("fps"))        return s->framerate;
            if (outputName == QLatin1String("bitrate"))    return s->bitrate;
            if (outputName == QLatin1String("channels"))   return s->channels;
            if (outputName == QLatin1String("samplerate")) return s->sampleRate;
            return {};
        }
        const Connection* next = nullptr;
        for (const auto& c : graph.connections())
            if (c->to.nodeId == nodeId && c->to.index == 0) { next = c.get(); break; }
        if (!next)
            return {};
        nodeId = next->from.nodeId;
    }
    return {};
}

/// Composes a File Output path from its name/directory/extension parts.
QString composeOutputPath(const QString& name, const QString& directory,
                          const QString& extension)
{
    if (name.isEmpty())
        return {};
    QString file = name;
    if (!extension.isEmpty())
        file += QLatin1Char('.') + extension;
    return directory.isEmpty() ? file : QDir(directory).filePath(file);
}

/// Sets the target element's `propName` from a source value. For `bitrate` the
/// value is bits/s; convert to the property's unit (bps vs kbps) from its range.
void applyValueToProperty(GstElement* element, const QString& typeId,
                          const QString& propName, const QString& sourceName,
                          double value, std::function<void(const QString&)> log)
{
    const std::vector<ElementProperty>& props = ElementProperties::forElement(typeId);
    const ElementProperty* chosen = nullptr;
    for (const ElementProperty& p : props)
        if (p.name == propName) { chosen = &p; break; }
    if (!chosen) {
        log(QStringLiteral("Inspector: “%1” has no property “%2”; ignoring.")
                .arg(typeId, propName));
        return;
    }

    double v = value;
    if (sourceName == QLatin1String("bitrate") && chosen->maximum < 1.0e7)
        v = value / 1000.0;   // source is bits/s, property expects kbit/s
    v = std::clamp(v, chosen->minimum, chosen->maximum);

    gst_util_set_object_arg(G_OBJECT(element), propName.toUtf8().constData(),
                            QString::number(static_cast<qint64>(v)).toUtf8().constData());
    log(QStringLiteral("Inspector: set %1.%2 = %3").arg(typeId, propName)
            .arg(static_cast<qint64>(v)));
}

} // namespace

PipelineRunner::PipelineRunner(QObject* parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->pollTimer.setInterval(150);
    connect(&d->pollTimer, &QTimer::timeout, this, &PipelineRunner::poll);
}

PipelineRunner::~PipelineRunner() { teardown(); }

bool PipelineRunner::isRunning() const { return d->running; }

bool PipelineRunner::run(const Graph& graph, QString* error)
{
    auto fail = [&](const QString& msg) { if (error) *error = msg; teardown(); return false; };

    if (d->running)
        stop();

    ensureGstInit();
    d->pipeline = gst_pipeline_new("avena");

    QHash<int, GstElement*> elementForNode;   // node id -> element to link into
    QHash<int, SourceContext*> sourceForNode; // node id -> its decodebin context
    int sinkCount = 0;

    // 1) Create one (or more) GStreamer elements per node.
    for (const auto& node : graph.nodes()) {
        if (node->typeId == QLatin1String("source.file")) {
            const QString location = node->properties.value(QStringLiteral("location")).toString();
            if (location.isEmpty())
                return fail(QStringLiteral("A File Source has no file assigned."));

            GstElement* filesrc = makeAdd(d->pipeline, "filesrc");
            GstElement* decodebin = makeAdd(d->pipeline, "decodebin");
            if (!filesrc || !decodebin)
                return fail(QStringLiteral("Missing core element (filesrc/decodebin)."));
            g_object_set(filesrc, "location", location.toUtf8().constData(), nullptr);
            gst_element_link(filesrc, decodebin);

            auto ctx = std::make_unique<SourceContext>();
            ctx->pipeline = d->pipeline;
            ctx->filesrc = filesrc;
            ctx->decodebin = decodebin;
            // Per-source time range, applied seek-free via the decoder pad probes
            // (empty start/end → -1 → no trim, i.e. the whole file).
            ctx->trimStart = parseTimeNs(node->properties.value(QStringLiteral("start")).toString());
            ctx->trimStop  = parseTimeNs(node->properties.value(QStringLiteral("end")).toString());
            if (ctx->trimStart >= 0 || ctx->trimStop >= 0)
                emit logMessage(QStringLiteral("Time range (%1): %2s → %3")
                    .arg(QFileInfo(location).fileName())
                    .arg((ctx->trimStart >= 0 ? ctx->trimStart : 0) / static_cast<double>(GST_SECOND), 0, 'f', 2)
                    .arg(ctx->trimStop >= 0
                         ? QStringLiteral("%1s").arg(ctx->trimStop / static_cast<double>(GST_SECOND), 0, 'f', 2)
                         : QStringLiteral("end")));
            g_signal_connect(decodebin, "pad-added", G_CALLBACK(onPadAdded), ctx.get());

            elementForNode.insert(node->id, decodebin);
            sourceForNode.insert(node->id, ctx.get());
            d->sources.push_back(std::move(ctx));
        } else if (isToolNode(*node)) {
            // Tools (e.g. the Stream Inspector) are passthrough markers in the
            // media path; they map to an identity element. Their effect is
            // applied separately (property binding).
            GstElement* element = makeAdd(d->pipeline, "identity");
            if (!element)
                return fail(QStringLiteral("Missing core element (identity)."));
            g_object_set(element, "silent", TRUE, nullptr);
            elementForNode.insert(node->id, element);
        } else {
            GstElement* element =
                gst_element_factory_make(node->typeId.toUtf8().constData(), nullptr);
            if (!element)
                return fail(QStringLiteral("Could not create element “%1”.").arg(node->typeId));
            gst_bin_add(GST_BIN(d->pipeline), element);
            applyProperties(element, *node);

            if (node->category == QLatin1String("Sinks"))
                ++sinkCount;   // File Output destination validated after binding
            elementForNode.insert(node->id, element);
        }
    }

    if (sinkCount == 0)
        return fail(QStringLiteral("The pipeline has no output (sink) node."));

    // Value (parameter) connections aren't GStreamer links — collect them so an
    // Inspector can drive a target node's property after the graph is built.
    struct ValueBinding {
        const Node* inspector;
        QString     outputName;   // which inspected property (bitrate, width…)
        GstElement* target;
        int         targetNodeId;
        QString     targetType;
        QString     targetProperty;
    };
    std::vector<ValueBinding> valueBindings;

    // 2) Realize connections.
    for (const auto& conn : graph.connections()) {
        const Node* a = graph.node(conn->from.nodeId);
        const Node* b = graph.node(conn->to.nodeId);
        if (!a || !b)
            continue;

        const Port* fromPort = a->port(PortDirection::Output, conn->from.index);
        const Port* toPort   = b->port(PortDirection::Input, conn->to.index);

        // Skip parameter (value) edges; record Inspector → property bindings.
        if ((fromPort && fromPort->mediaType == MediaType::Value) ||
            (toPort && toPort->mediaType == MediaType::Value)) {
            if (a->typeId == QLatin1String("tool.inspector") && fromPort &&
                !conn->targetProperty.isEmpty()) {
                if (GstElement* tgt = elementForNode.value(b->id, nullptr))
                    valueBindings.push_back({a, fromPort->name, tgt, b->id,
                                             b->typeId, conn->targetProperty});
            }
            continue;
        }

        GstElement* dst = elementForNode.value(b->id, nullptr);
        if (!dst)
            continue;

        const bool dstIsMuxer  = b->category == QLatin1String("Muxers");
        const bool needConvert = b->category == QLatin1String("Encoders");
        // Media passing through a tool node carries an "Any" port; fall back to
        // the destination port's type so the right converter still gets inserted.
        MediaType type = fromPort ? fromPort->mediaType : MediaType::Any;
        if (type == MediaType::Any && toPort && toPort->mediaType != MediaType::Any)
            type = toPort->mediaType;

        if (SourceContext* ctx = sourceForNode.value(a->id, nullptr)) {
            // Linked later, when decodebin exposes the matching stream.
            ctx->pending.push_back({type, dst, dstIsMuxer, needConvert});
        } else {
            GstElement* src = elementForNode.value(a->id, nullptr);
            if (!src)
                continue;
            GstPad* srcpad = gst_element_get_static_pad(src, "src");
            if (!srcpad)
                return fail(QStringLiteral("“%1” has no source pad to link.").arg(a->typeId));
            const bool ok = linkPadToDst(d->pipeline, srcpad, type, needConvert,
                                         dst, dstIsMuxer, /*dynamic=*/false);
            gst_object_unref(srcpad);
            if (!ok)
                return fail(QStringLiteral("Could not link “%1” → “%2”.")
                                .arg(a->typeId, b->typeId));
        }
    }

    // 3) Resolve Inspector value bindings: source property → target property.
    auto log = [this](const QString& s) { emit logMessage(s); };
    // Synthetic File Output parts driven by bindings, keyed by node id.
    QHash<int, QHash<QString, QString>> fileSinkOverrides;

    for (const ValueBinding& bind : valueBindings) {
        const QVariant value = inspectorValue(graph, *bind.inspector, bind.outputName);
        if (!value.isValid()) {
            emit logMessage(QStringLiteral("Inspector: %1 unavailable; keeping %2 default.")
                                .arg(bind.outputName, bind.targetProperty));
            continue;
        }

        const QString& p = bind.targetProperty;
        const bool synthetic = bind.targetType == QLatin1String("filesink") &&
            (p == QLatin1String("name") || p == QLatin1String("directory") ||
             p == QLatin1String("extension"));

        if (synthetic) {
            fileSinkOverrides[bind.targetNodeId][p] = value.toString();
        } else if (value.typeId() == QMetaType::QString) {
            gst_util_set_object_arg(G_OBJECT(bind.target), p.toUtf8().constData(),
                                    value.toString().toUtf8().constData());
            log(QStringLiteral("Inspector: set %1.%2 = %3")
                    .arg(bind.targetType, p, value.toString()));
        } else {
            applyValueToProperty(bind.target, bind.targetType, p, bind.outputName,
                                 value.toDouble(), log);
        }
    }

    // Compose each File Output's location from name/directory/extension (its
    // static properties, overridden by any Inspector bindings).
    for (const auto& node : graph.nodes()) {
        if (node->typeId != QLatin1String("filesink"))
            continue;
        GstElement* el = elementForNode.value(node->id, nullptr);
        if (!el)
            continue;

        const auto& ov = fileSinkOverrides.value(node->id);
        auto part = [&](const QString& key) {
            return ov.contains(key) ? ov.value(key)
                                    : node->properties.value(key).toString();
        };
        QString location = node->properties.value(QStringLiteral("location")).toString();
        const QString composed = composeOutputPath(part(QStringLiteral("name")),
                                                   part(QStringLiteral("directory")),
                                                   part(QStringLiteral("extension")));
        if (!composed.isEmpty())
            location = composed;
        if (location.isEmpty())
            return fail(QStringLiteral("A File Output has no destination file."));
        g_object_set(el, "location", location.toUtf8().constData(), nullptr);
        emit logMessage(QStringLiteral("Output: %1").arg(location));
    }

    // 4) Go. (Per-source time-range trimming is applied by the decoder pad
    // probes; see the File Source branch above.)
    d->running = true;
    if (gst_element_set_state(d->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        return fail(QStringLiteral("Failed to start the pipeline."));

    d->pollTimer.start();
    emit started();
    return true;
}

void PipelineRunner::poll()
{
    if (!d->pipeline)
        return;

    GstBus* bus = gst_element_get_bus(d->pipeline);
    while (GstMessage* msg = gst_bus_pop(bus)) {
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            const QString text = err ? QString::fromUtf8(err->message)
                                     : QStringLiteral("Unknown error");
            emit logMessage(QStringLiteral("ERROR: %1").arg(text));
            if (dbg) { emit logMessage(QString::fromUtf8(dbg)); g_free(dbg); }
            if (err) g_error_free(err);
            gst_message_unref(msg);
            gst_object_unref(bus);
            finishWith(false, text);
            return;
        }
        case GST_MESSAGE_EOS:
            gst_message_unref(msg);
            gst_object_unref(bus);
            emit logMessage(QStringLiteral("Done."));
            finishWith(true, {});
            return;
        case GST_MESSAGE_WARNING: {
            GError* err = nullptr;
            gchar* dbg = nullptr;
            gst_message_parse_warning(msg, &err, &dbg);
            if (err) { emit logMessage(QStringLiteral("WARN: %1").arg(QString::fromUtf8(err->message))); g_error_free(err); }
            if (dbg) g_free(dbg);
            break;
        }
        default:
            break;
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);

    gint64 pos = 0, dur = 0;
    if (gst_element_query_position(d->pipeline, GST_FORMAT_TIME, &pos) &&
        gst_element_query_duration(d->pipeline, GST_FORMAT_TIME, &dur) && dur > 0) {
        emit progress(static_cast<double>(pos) / static_cast<double>(dur));
    }
}

void PipelineRunner::finishWith(bool success, const QString& error)
{
    d->pollTimer.stop();
    if (success)
        emit progress(1.0);
    teardown();
    emit finished(success, error);
}

void PipelineRunner::stop()
{
    if (!d->running)
        return;
    d->pollTimer.stop();
    teardown();
    emit finished(false, QStringLiteral("Stopped."));
}

void PipelineRunner::teardown()
{
    if (d->pipeline) {
        gst_element_set_state(d->pipeline, GST_STATE_NULL);
        gst_object_unref(d->pipeline);
        d->pipeline = nullptr;
    }
    d->sources.clear();
    d->running = false;
}

#else // !AVENA_HAVE_GSTREAMER

struct PipelineRunner::Private {};

PipelineRunner::PipelineRunner(QObject* parent)
    : QObject(parent), d(std::make_unique<Private>()) {}
PipelineRunner::~PipelineRunner() = default;
bool PipelineRunner::isRunning() const { return false; }

bool PipelineRunner::run(const Graph&, QString* error)
{
    if (error) *error = QStringLiteral("This build has no GStreamer backend.");
    return false;
}

void PipelineRunner::stop() {}
void PipelineRunner::poll() {}
void PipelineRunner::finishWith(bool, const QString&) {}
void PipelineRunner::teardown() {}

#endif

} // namespace ab
