#pragma once

#include <QList>
#include <QString>

namespace ab {

/// One elementary stream (video, audio or subtitle) inside a media file.
struct MediaStreamInfo {
    enum class Kind { Video, Audio, Subtitle, Other };

    Kind    kind = Kind::Other;
    QString codec;
    int     bitrate = 0;     ///< bits/s, 0 if unknown

    // Video
    int    width = 0;
    int    height = 0;
    double framerate = 0.0;

    // Audio
    int channels = 0;
    int sampleRate = 0;      ///< Hz

    // Subtitle
    QString language;        ///< ISO code if known
};

/// Result of probing a media file. `valid` is false when the file could not be
/// analysed (missing, unsupported, or built without GStreamer); `error` then
/// carries a human-readable reason.
struct MediaInfo {
    bool    valid = false;
    QString error;

    qint64  fileSize = 0;        ///< bytes
    qint64  durationNs = 0;      ///< nanoseconds, 0 if unknown
    QString container;           ///< e.g. "Quicktime", "Matroska"

    QList<MediaStreamInfo> streams;
};

namespace MediaProbe {

/// True if this build can analyse media (compiled with GStreamer).
bool available();

/// Probes `path`, caching results per (path, size, mtime). Synchronous, with a
/// short internal timeout.
MediaInfo probe(const QString& path);

} // namespace MediaProbe
} // namespace ab
