#include "app/MainWindow.h"
#include "engine/PipelineRunner.h"
#include "graph/Graph.h"
#include "graph/GraphSerializer.h"

#include <QApplication>

#include <cstdio>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("avena"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("avena"));

    // Headless batch mode: `avena --run pipeline.abk` builds and runs the
    // pipeline without a window, then exits 0 on success.
    const QStringList args = app.arguments();
    const int runIndex = args.indexOf(QStringLiteral("--run"));
    if (runIndex >= 0 && runIndex + 1 < args.size()) {
        const QString path = args.at(runIndex + 1);

        ab::Graph graph;
        QString error;
        if (!ab::GraphSerializer::loadFromFile(graph, path, &error)) {
            std::fprintf(stderr, "load failed: %s\n", qUtf8Printable(error));
            return 2;
        }

        ab::PipelineRunner runner;
        int exitCode = 1;
        QObject::connect(&runner, &ab::PipelineRunner::logMessage,
                         [](const QString& t) { std::fprintf(stderr, "%s\n", qUtf8Printable(t)); });
        QObject::connect(&runner, &ab::PipelineRunner::finished,
                         [&](bool ok, const QString& err) {
                             exitCode = ok ? 0 : 1;
                             if (!ok && !err.isEmpty())
                                 std::fprintf(stderr, "error: %s\n", qUtf8Printable(err));
                             QCoreApplication::quit();
                         });
        if (!runner.run(graph, &error)) {
            std::fprintf(stderr, "run failed: %s\n", qUtf8Printable(error));
            return 2;
        }
        app.exec();
        return exitCode;
    }

    // No global stylesheet: use the desktop's native Qt style (Breeze on KDE)
    // and icon theme so the app matches the rest of the environment.
    ab::MainWindow window;
    window.show();

    return app.exec();
}
