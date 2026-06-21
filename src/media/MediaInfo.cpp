#include "media/MediaInfo.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>

#ifdef AVENA_HAVE_GSTREAMER
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#endif

namespace ab {
namespace MediaProbe {

bool available()
{
#ifdef AVENA_HAVE_GSTREAMER
    return true;
#else
    return false;
#endif
}

namespace {

QString cacheKey(const QFileInfo& fi)
{
    return QStringLiteral("%1|%2|%3")
        .arg(fi.absoluteFilePath())
        .arg(fi.size())
        .arg(fi.lastModified().toMSecsSinceEpoch());
}

#ifdef AVENA_HAVE_GSTREAMER

void ensureGstInit()
{
    static bool initialised = false;
    if (!initialised) {
        gst_init(nullptr, nullptr);
        initialised = true;
    }
}

QString codecFromStream(GstDiscovererStreamInfo* stream)
{
    QString result;
    if (GstCaps* caps = gst_discoverer_stream_info_get_caps(stream)) {
        if (gchar* desc = gst_pb_utils_get_codec_description(caps)) {
            result = QString::fromUtf8(desc);
            g_free(desc);
        }
        gst_caps_unref(caps);
    }
    return result;
}

QString resultMessage(GstDiscovererResult result)
{
    switch (result) {
    case GST_DISCOVERER_OK:              return {};
    case GST_DISCOVERER_URI_INVALID:     return QStringLiteral("Invalid URI.");
    case GST_DISCOVERER_ERROR:           return QStringLiteral("Could not analyse file.");
    case GST_DISCOVERER_TIMEOUT:         return QStringLiteral("Analysis timed out.");
    case GST_DISCOVERER_BUSY:            return QStringLiteral("Discoverer busy.");
    case GST_DISCOVERER_MISSING_PLUGINS: return QStringLiteral("Missing GStreamer plugins for this format.");
    }
    return QStringLiteral("Unknown error.");
}

MediaInfo probeWithGStreamer(const QString& path, MediaInfo mi)
{
    ensureGstInit();

    GError* error = nullptr;
    GstDiscoverer* discoverer = gst_discoverer_new(5 * GST_SECOND, &error);
    if (!discoverer) {
        mi.error = error ? QString::fromUtf8(error->message)
                         : QStringLiteral("Failed to create discoverer.");
        g_clear_error(&error);
        return mi;
    }

    gchar* uri = gst_filename_to_uri(path.toUtf8().constData(), &error);
    if (!uri) {
        mi.error = error ? QString::fromUtf8(error->message)
                         : QStringLiteral("Failed to build URI.");
        g_clear_error(&error);
        g_object_unref(discoverer);
        return mi;
    }

    GstDiscovererInfo* info = gst_discoverer_discover_uri(discoverer, uri, &error);
    g_free(uri);

    if (!info) {
        mi.error = error ? QString::fromUtf8(error->message)
                         : QStringLiteral("Analysis failed.");
        g_clear_error(&error);
        g_object_unref(discoverer);
        return mi;
    }

    const GstDiscovererResult result = gst_discoverer_info_get_result(info);
    if (result != GST_DISCOVERER_OK) {
        mi.error = resultMessage(result);
    } else {
        mi.valid = true;
        mi.durationNs = static_cast<qint64>(gst_discoverer_info_get_duration(info));

        if (GstDiscovererStreamInfo* top = gst_discoverer_info_get_stream_info(info)) {
            mi.container = codecFromStream(top);
            gst_discoverer_stream_info_unref(top);
        }

        GList* videos = gst_discoverer_info_get_video_streams(info);
        for (GList* it = videos; it; it = it->next) {
            auto* v = static_cast<GstDiscovererVideoInfo*>(it->data);
            MediaStreamInfo s;
            s.kind   = MediaStreamInfo::Kind::Video;
            s.width  = static_cast<int>(gst_discoverer_video_info_get_width(v));
            s.height = static_cast<int>(gst_discoverer_video_info_get_height(v));
            const guint num = gst_discoverer_video_info_get_framerate_num(v);
            const guint den = gst_discoverer_video_info_get_framerate_denom(v);
            s.framerate = den ? static_cast<double>(num) / static_cast<double>(den) : 0.0;
            s.bitrate = static_cast<int>(gst_discoverer_video_info_get_bitrate(v));
            s.codec = codecFromStream(reinterpret_cast<GstDiscovererStreamInfo*>(v));
            mi.streams.push_back(s);
        }
        gst_discoverer_stream_info_list_free(videos);

        GList* audios = gst_discoverer_info_get_audio_streams(info);
        for (GList* it = audios; it; it = it->next) {
            auto* a = static_cast<GstDiscovererAudioInfo*>(it->data);
            MediaStreamInfo s;
            s.kind       = MediaStreamInfo::Kind::Audio;
            s.channels   = static_cast<int>(gst_discoverer_audio_info_get_channels(a));
            s.sampleRate = static_cast<int>(gst_discoverer_audio_info_get_sample_rate(a));
            s.bitrate    = static_cast<int>(gst_discoverer_audio_info_get_bitrate(a));
            s.codec = codecFromStream(reinterpret_cast<GstDiscovererStreamInfo*>(a));
            mi.streams.push_back(s);
        }
        gst_discoverer_stream_info_list_free(audios);

        GList* subs = gst_discoverer_info_get_subtitle_streams(info);
        for (GList* it = subs; it; it = it->next) {
            auto* sub = static_cast<GstDiscovererSubtitleInfo*>(it->data);
            MediaStreamInfo s;
            s.kind = MediaStreamInfo::Kind::Subtitle;
            if (const gchar* lang = gst_discoverer_subtitle_info_get_language(sub))
                s.language = QString::fromUtf8(lang);
            s.codec = codecFromStream(reinterpret_cast<GstDiscovererStreamInfo*>(sub));
            mi.streams.push_back(s);
        }
        gst_discoverer_stream_info_list_free(subs);
    }

    gst_discoverer_info_unref(info);
    g_object_unref(discoverer);
    return mi;
}

#endif // AVENA_HAVE_GSTREAMER

} // namespace

MediaInfo probe(const QString& path)
{
    static QHash<QString, MediaInfo> cache;

    MediaInfo mi;
    const QFileInfo fi(path);
    if (path.isEmpty() || !fi.exists()) {
        mi.error = QStringLiteral("File not found.");
        return mi;
    }
    mi.fileSize = fi.size();

    const QString key = cacheKey(fi);
    if (auto it = cache.constFind(key); it != cache.constEnd())
        return it.value();

#ifdef AVENA_HAVE_GSTREAMER
    mi = probeWithGStreamer(path, mi);
#else
    mi.error = QStringLiteral("Built without GStreamer; only file size is available.");
#endif

    cache.insert(key, mi);
    return mi;
}

} // namespace MediaProbe
} // namespace ab
