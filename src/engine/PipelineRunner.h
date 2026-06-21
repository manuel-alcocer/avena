#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace ab {

class Graph;

/// Builds and runs a GStreamer pipeline from the logical graph.
///
/// File Source nodes become `filesrc ! decodebin` (dynamic pads are linked by
/// media type); encoder inputs get `videoconvert` / `audioconvert+audioresample`
/// inserted automatically; muxer inputs use request pads. Progress and bus
/// messages are surfaced through signals.
class PipelineRunner : public QObject {
    Q_OBJECT

public:
    explicit PipelineRunner(QObject* parent = nullptr);
    ~PipelineRunner() override;

    /// True if this build can execute pipelines (compiled with GStreamer).
    static bool available();

    [[nodiscard]] bool isRunning() const;

    /// Builds and starts the pipeline. Returns false (with `error`) if the graph
    /// is invalid or an element can't be created.
    bool run(const Graph& graph, QString* error);

    /// Stops a running pipeline (emits finished with success=false).
    void stop();

signals:
    void started();
    /// Completion ratio in [0, 1]; emitted only when a duration is known.
    void progress(double fraction);
    void logMessage(const QString& text);
    void finished(bool success, const QString& error);

private:
    void poll();
    void finishWith(bool success, const QString& error);
    void teardown();

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace ab
