#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace ab {

/// Plays a local media file in its own window via GStreamer `playbin`, so the
/// user can preview a File Source's input. Independent from the transcoding
/// pipeline runner.
class MediaPreview : public QObject {
    Q_OBJECT

public:
    explicit MediaPreview(QObject* parent = nullptr);
    ~MediaPreview() override;

    static bool available();
    [[nodiscard]] bool isPlaying() const;

    /// Starts (or restarts) playback of `filePath`. Returns false with `error`
    /// if it could not start.
    bool play(const QString& filePath, QString* error = nullptr);
    void stop();

signals:
    void started();
    void finished();
    void logMessage(const QString& text);

private:
    void poll();

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace ab
