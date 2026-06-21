#include "graph/NodeCatalog.h"

#include <algorithm>

#ifdef AVENA_HAVE_GSTREAMER
#include <gst/gst.h>
#endif

namespace ab {

namespace {
// Category colours (header tint of the nodes).
const QColor kSourceColor (0x5C, 0x6B, 0xC0);
const QColor kFilterColor (0x26, 0xA6, 0x9A);
const QColor kEncoderColor(0xEF, 0x53, 0x50);
const QColor kMuxerColor  (0xAB, 0x47, 0xBC);
const QColor kSinkColor   (0x78, 0x90, 0x9C);

const QColor kToolColor   (0xFB, 0x8C, 0x00);

const QString kSources  = QStringLiteral("Sources");
const QString kTools    = QStringLiteral("Tools");
const QString kFilters  = QStringLiteral("Filters");
const QString kEncoders = QStringLiteral("Encoders");
const QString kMuxers   = QStringLiteral("Muxers");
const QString kSinks    = QStringLiteral("Sinks");
} // namespace

void NodeCatalog::registerSpec(NodeTypeSpec spec)
{
    m_specs.push_back(std::move(spec));
}

namespace {

/// The fixed "Sources" entry. The File Source is special: its output ports are
/// rebuilt from the probed media streams when a file is assigned.
NodeTypeSpec fileSourceSpec()
{
    return NodeTypeSpec{
        .typeId      = QStringLiteral("source.file"),
        .displayName = QStringLiteral("File Source"),
        .longName    = QStringLiteral("File Source"),
        .description = QStringLiteral("Reads a media file and decodes its "
                                      "video, audio and subtitle streams."),
        .category    = kSources,
        .color       = kSourceColor,
        .inputs      = {},
        .outputs     = { {QStringLiteral("video"), MediaType::Video},
                         {QStringLiteral("audio"), MediaType::Audio} },
        .defaultProperties = { {QStringLiteral("location"), QString()} },
    };
}

/// Built-in "Tools" nodes that don't map to a single GStreamer element.
std::vector<NodeTypeSpec> toolSpecs()
{
    NodeTypeSpec timeRange{
        .typeId      = QStringLiteral("tool.timerange"),
        .displayName = QStringLiteral("Time Range"),
        .longName    = QStringLiteral("Time Range"),
        .description = QStringLiteral("Restrict the pipeline to a [start, end] "
            "time window. Toggle Bypass to process the whole file. Handy for "
            "quickly testing a pipeline on a short segment."),
        .category    = kTools,
        .color       = kToolColor,
        .inputs      = { {QStringLiteral("in"),  MediaType::Any} },
        .outputs     = { {QStringLiteral("out"), MediaType::Any} },
        .defaultProperties = {
            {QStringLiteral("start"),  QString()},
            {QStringLiteral("end"),    QString()},
            {QStringLiteral("bypass"), false},
        },
    };

    NodeTypeSpec inspector{
        .typeId      = QStringLiteral("tool.inspector"),
        .displayName = QStringLiteral("Inspector"),
        .longName    = QStringLiteral("Stream Inspector"),
        .description = QStringLiteral("Passes a stream through unchanged and "
            "exposes its properties (bitrate, resolution, fps, channels…) as "
            "value outputs you can wire into other nodes' aux inputs. The "
            "available outputs follow the connected stream type."),
        .category    = kTools,
        .color       = kToolColor,
        .inputs      = { {QStringLiteral("in"),  MediaType::Any} },
        .outputs     = { {QStringLiteral("out"), MediaType::Any} },
        .defaultProperties = {},
    };

    return {timeRange, inspector};
}

} // namespace

std::vector<PortSpec> inspectorValueOutputs(MediaType type)
{
    auto v = [](const QString& n) { return PortSpec{n, MediaType::Value}; };
    if (type != MediaType::Video && type != MediaType::Audio)
        return {};   // need a connected stream to know the source

    // File-level outputs (string), available for any connected stream.
    std::vector<PortSpec> outs = { v(QStringLiteral("filename")),
                                   v(QStringLiteral("directory")),
                                   v(QStringLiteral("extension")) };
    // Stream-level outputs (numeric).
    if (type == MediaType::Video) {
        outs.push_back(v(QStringLiteral("width")));
        outs.push_back(v(QStringLiteral("height")));
        outs.push_back(v(QStringLiteral("fps")));
        outs.push_back(v(QStringLiteral("bitrate")));
    } else {
        outs.push_back(v(QStringLiteral("channels")));
        outs.push_back(v(QStringLiteral("samplerate")));
        outs.push_back(v(QStringLiteral("bitrate")));
    }
    return outs;
}

#ifdef AVENA_HAVE_GSTREAMER

namespace {

void ensureGstInit()
{
    static bool done = false;
    if (!done) {
        gst_init(nullptr, nullptr);
        done = true;
    }
}

MediaType mediaTypeFromCapsName(const QString& name)
{
    if (name.startsWith(QLatin1String("video/")) ||
        name.startsWith(QLatin1String("image/")))
        return MediaType::Video;
    if (name.startsWith(QLatin1String("audio/")))
        return MediaType::Audio;
    if (name.startsWith(QLatin1String("text/")) ||
        name.startsWith(QLatin1String("subtitle/")) ||
        name.startsWith(QLatin1String("application/x-subtitle")))
        return MediaType::Subtitle;
    if (name.startsWith(QLatin1String("application/")))
        return MediaType::Data;
    return MediaType::Any;
}

MediaType mediaTypeFromStaticCaps(GstStaticCaps* staticCaps)
{
    MediaType type = MediaType::Any;
    if (GstCaps* caps = gst_static_caps_get(staticCaps)) {
        if (!gst_caps_is_any(caps) && gst_caps_get_size(caps) > 0) {
            const GstStructure* s = gst_caps_get_structure(caps, 0);
            type = mediaTypeFromCapsName(
                QString::fromUtf8(gst_structure_get_name(s)));
        }
        gst_caps_unref(caps);
    }
    return type;
}

/// Turns a pad template name ("video_%u", "sink", "src_%d") into a label.
QString cleanPadName(const QString& templateName, PortDirection dir)
{
    QString name = templateName;
    name.replace(QLatin1String("_%u"), QString());
    name.replace(QLatin1String("_%d"), QString());
    name.replace(QLatin1String("%u"), QString());
    name.replace(QLatin1String("%d"), QString());
    name.replace(QLatin1String("%s"), QString());
    while (name.endsWith(QLatin1Char('_')))
        name.chop(1);
    if (name.isEmpty())
        name = dir == PortDirection::Input ? QStringLiteral("in")
                                           : QStringLiteral("out");
    return name;
}

void collectPorts(GstElementFactory* factory, NodeTypeSpec& spec)
{
    const GList* templates = gst_element_factory_get_static_pad_templates(factory);
    for (const GList* it = templates; it; it = it->next) {
        auto* tmpl = static_cast<GstStaticPadTemplate*>(it->data);
        const PortDirection dir = tmpl->direction == GST_PAD_SRC
            ? PortDirection::Output : PortDirection::Input;
        const MediaType type = mediaTypeFromStaticCaps(&tmpl->static_caps);
        const QString name = cleanPadName(
            QString::fromUtf8(tmpl->name_template), dir);

        if (dir == PortDirection::Input)
            spec.inputs.push_back({name, type});
        else
            spec.outputs.push_back({name, type});
    }
}

/// Classifies a factory by its GStreamer "klass" metadata. Returns false to
/// skip elements outside the four processing categories we expose.
bool categoryForKlass(const QString& klass, QString& category, QColor& color)
{
    // Order matters: an encoder is never a muxer/sink/filter.
    if (klass.contains(QLatin1String("Encoder"))) {
        category = kEncoders; color = kEncoderColor; return true;
    }
    if (klass.contains(QLatin1String("Muxer"))) {     // excludes "Demuxer"
        category = kMuxers;   color = kMuxerColor;   return true;
    }
    if (klass.contains(QLatin1String("Sink"))) {
        category = kSinks;    color = kSinkColor;    return true;
    }
    if (klass.contains(QLatin1String("Filter")) ||
        klass.contains(QLatin1String("Effect")) ||
        klass.contains(QLatin1String("Converter")) ||
        klass.contains(QLatin1String("Scaler"))) {
        category = kFilters;  color = kFilterColor;  return true;
    }
    return false;
}

} // namespace

void NodeCatalog::populateFromGStreamer()
{
    ensureGstInit();

    GstRegistry* registry = gst_registry_get();
    GList* features = gst_registry_get_feature_list(registry, GST_TYPE_ELEMENT_FACTORY);

    std::vector<NodeTypeSpec> discovered;
    for (GList* it = features; it; it = it->next) {
        auto* factory = GST_ELEMENT_FACTORY(it->data);

        const QString klass = QString::fromUtf8(
            gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_KLASS));

        QString category;
        QColor  color;
        if (!categoryForKlass(klass, category, color))
            continue;

        const QString name = QString::fromUtf8(
            gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)));

        NodeTypeSpec spec;
        spec.typeId      = name;       // == the GStreamer element name
        spec.displayName = name;
        spec.category    = category;
        spec.color       = color;
        if (const gchar* longName = gst_element_factory_get_metadata(
                factory, GST_ELEMENT_METADATA_LONGNAME))
            spec.longName = QString::fromUtf8(longName);
        if (const gchar* desc = gst_element_factory_get_metadata(
                factory, GST_ELEMENT_METADATA_DESCRIPTION))
            spec.description = QString::fromUtf8(desc);
        collectPorts(factory, spec);

        // A muxer's source pad advertises a container caps (e.g. video/quicktime);
        // treat it as opaque data so it only feeds sinks/muxers.
        if (category == kMuxers)
            for (PortSpec& out : spec.outputs)
                out.mediaType = MediaType::Data;

        // Every node that accepts input gets a generic auxiliary value input so
        // an Inspector can drive any of its properties (chosen at connect time).
        if (!spec.inputs.empty())
            spec.inputs.push_back({QStringLiteral("aux"), MediaType::Value});

        // The File Output composes its path from name/directory/extension (any
        // of which an Inspector can drive); falls back to `location` if set.
        if (name == QLatin1String("filesink")) {
            spec.defaultProperties.insert(QStringLiteral("name"), QString());
            spec.defaultProperties.insert(QStringLiteral("directory"), QString());
            spec.defaultProperties.insert(QStringLiteral("extension"), QString());
        }

        discovered.push_back(std::move(spec));
    }
    gst_plugin_feature_list_free(features);

    // Stable order: by category (our canonical order), then element name.
    const auto categoryRank = [](const QString& c) {
        if (c == kFilters)  return 0;
        if (c == kEncoders) return 1;
        if (c == kMuxers)   return 2;
        if (c == kSinks)    return 3;
        return 4;
    };
    std::ranges::sort(discovered, [&](const NodeTypeSpec& a, const NodeTypeSpec& b) {
        const int ra = categoryRank(a.category), rb = categoryRank(b.category);
        if (ra != rb) return ra < rb;
        return a.displayName.compare(b.displayName, Qt::CaseInsensitive) < 0;
    });

    for (NodeTypeSpec& spec : discovered)
        registerSpec(std::move(spec));
}

#endif // AVENA_HAVE_GSTREAMER

void NodeCatalog::populateStatic()
{
    // Fallback set used when built without GStreamer.
    auto vid = [](const QString& n) { return PortSpec{n, MediaType::Video}; };
    auto aud = [](const QString& n) { return PortSpec{n, MediaType::Audio}; };

    registerSpec({ .typeId = QStringLiteral("filter.videoscale"),
        .displayName = QStringLiteral("Scale"), .category = kFilters,
        .color = kFilterColor, .inputs = {vid("video")}, .outputs = {vid("video")},
        .defaultProperties = {{QStringLiteral("width"), 1280}, {QStringLiteral("height"), 720}} });
    registerSpec({ .typeId = QStringLiteral("encoder.h264"),
        .displayName = QStringLiteral("H.264 Encoder"), .category = kEncoders,
        .color = kEncoderColor, .inputs = {vid("video")}, .outputs = {vid("video")},
        .defaultProperties = {{QStringLiteral("bitrate"), 4000}} });
    registerSpec({ .typeId = QStringLiteral("encoder.aac"),
        .displayName = QStringLiteral("AAC Encoder"), .category = kEncoders,
        .color = kEncoderColor, .inputs = {aud("audio")}, .outputs = {aud("audio")},
        .defaultProperties = {{QStringLiteral("bitrate"), 192}} });
    registerSpec({ .typeId = QStringLiteral("mux.mp4"),
        .displayName = QStringLiteral("MP4 Muxer"), .category = kMuxers,
        .color = kMuxerColor, .inputs = {vid("video"), aud("audio")},
        .outputs = {{QStringLiteral("file"), MediaType::Data}}, .defaultProperties = {} });
    registerSpec({ .typeId = QStringLiteral("sink.file"),
        .displayName = QStringLiteral("File Output"), .category = kSinks,
        .color = kSinkColor, .inputs = {{QStringLiteral("file"), MediaType::Data}},
        .outputs = {}, .defaultProperties = {{QStringLiteral("location"), QString()}} });
}

NodeCatalog::NodeCatalog()
{
    registerSpec(fileSourceSpec());
    for (NodeTypeSpec& spec : toolSpecs())
        registerSpec(std::move(spec));

#ifdef AVENA_HAVE_GSTREAMER
    populateFromGStreamer();
#else
    populateStatic();
#endif
}

const NodeCatalog& NodeCatalog::instance()
{
    static const NodeCatalog catalog;
    return catalog;
}

const NodeTypeSpec* NodeCatalog::find(const QString& typeId) const
{
    auto it = std::ranges::find_if(m_specs,
        [&](const NodeTypeSpec& s) { return s.typeId == typeId; });
    return it == m_specs.end() ? nullptr : &(*it);
}

std::vector<QString> NodeCatalog::categories() const
{
    std::vector<QString> result;
    for (const NodeTypeSpec& s : m_specs) {
        if (std::ranges::find(result, s.category) == result.end())
            result.push_back(s.category);
    }
    return result;
}

} // namespace ab
