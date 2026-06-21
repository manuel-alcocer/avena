#include "engine/MediaPreview.h"

#include <QFileInfo>
#include <QTimer>

#ifdef AVENA_HAVE_GSTREAMER
#include <gst/gst.h>
#endif

namespace ab {

bool MediaPreview::available()
{
#ifdef AVENA_HAVE_GSTREAMER
    return true;
#else
    return false;
#endif
}

#ifdef AVENA_HAVE_GSTREAMER

struct MediaPreview::Private {
    GstElement* playbin = nullptr;
    QTimer      pollTimer;
};

namespace {
void ensureGstInit()
{
    static bool done = false;
    if (!done) { gst_init(nullptr, nullptr); done = true; }
}
} // namespace

MediaPreview::MediaPreview(QObject* parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->pollTimer.setInterval(200);
    connect(&d->pollTimer, &QTimer::timeout, this, &MediaPreview::poll);
}

MediaPreview::~MediaPreview() { stop(); }

bool MediaPreview::isPlaying() const { return d->playbin != nullptr; }

bool MediaPreview::play(const QString& filePath, QString* error)
{
    stop();
    ensureGstInit();

    const QFileInfo fi(filePath);
    if (filePath.isEmpty() || !fi.exists()) {
        if (error) *error = QStringLiteral("File not found.");
        return false;
    }

    d->playbin = gst_element_factory_make("playbin", "preview");
    if (!d->playbin) {
        if (error) *error = QStringLiteral("playbin is not available.");
        return false;
    }

    GError* uriError = nullptr;
    gchar* uri = gst_filename_to_uri(filePath.toUtf8().constData(), &uriError);
    if (!uri) {
        if (error) *error = uriError ? QString::fromUtf8(uriError->message)
                                     : QStringLiteral("Bad file path.");
        g_clear_error(&uriError);
        gst_object_unref(d->playbin);
        d->playbin = nullptr;
        return false;
    }
    g_object_set(d->playbin, "uri", uri, nullptr);
    g_free(uri);

    if (gst_element_set_state(d->playbin, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        if (error) *error = QStringLiteral("Could not start playback.");
        stop();
        return false;
    }

    d->pollTimer.start();
    emit started();
    return true;
}

void MediaPreview::poll()
{
    if (!d->playbin)
        return;

    GstBus* bus = gst_element_get_bus(d->playbin);
    while (GstMessage* msg = gst_bus_pop(bus)) {
        const GstMessageType type = GST_MESSAGE_TYPE(msg);
        if (type == GST_MESSAGE_ERROR) {
            GError* err = nullptr;
            gst_message_parse_error(msg, &err, nullptr);
            if (err) {
                emit logMessage(QStringLiteral("Preview error: %1")
                                    .arg(QString::fromUtf8(err->message)));
                g_error_free(err);
            }
            gst_message_unref(msg);
            gst_object_unref(bus);
            stop();
            return;
        }
        if (type == GST_MESSAGE_EOS) {
            gst_message_unref(msg);
            gst_object_unref(bus);
            stop();
            return;
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
}

void MediaPreview::stop()
{
    const bool was = d->playbin != nullptr;
    d->pollTimer.stop();
    if (d->playbin) {
        gst_element_set_state(d->playbin, GST_STATE_NULL);
        gst_object_unref(d->playbin);
        d->playbin = nullptr;
    }
    if (was)
        emit finished();
}

#else // !AVENA_HAVE_GSTREAMER

struct MediaPreview::Private {};

MediaPreview::MediaPreview(QObject* parent)
    : QObject(parent), d(std::make_unique<Private>()) {}
MediaPreview::~MediaPreview() = default;
bool MediaPreview::isPlaying() const { return false; }
bool MediaPreview::play(const QString&, QString* error)
{
    if (error) *error = QStringLiteral("This build has no GStreamer backend.");
    return false;
}
void MediaPreview::stop() {}
void MediaPreview::poll() {}

#endif

} // namespace ab
