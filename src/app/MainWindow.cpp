#include "app/MainWindow.h"

#include "app/InfoPanel.h"
#include "app/NodePalette.h"
#include "app/PropertiesPanel.h"
#include "app/TemplateLibrary.h"
#include "app/TemplatePanel.h"
#include "canvas/NodeEditorScene.h"
#include "canvas/NodeEditorView.h"
#include "engine/MediaPreview.h"
#include "engine/PipelineRunner.h"
#include "graph/Graph.h"
#include "graph/GraphSerializer.h"
#include "graph/Node.h"

#include <QApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace ab {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    m_graph = new Graph(this);

    m_scene = new NodeEditorScene(this);
    m_scene->setGraph(m_graph);

    m_view = new NodeEditorView(m_scene, this);
    setCentralWidget(m_view);

    m_runner = new PipelineRunner(this);
    m_preview = new MediaPreview(this);

    createActions();
    createDocks();
    createPipelineDock();

    connect(m_runner, &PipelineRunner::started, this, [this] {
        m_runAction->setText(tr("Stop"));
        m_runAction->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-stop"),
                                              style()->standardIcon(QStyle::SP_MediaStop)));
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
        statusBar()->showMessage(tr("Running…"));
    });
    connect(m_runner, &PipelineRunner::progress, this, [this](double f) {
        m_progress->setValue(static_cast<int>(f * 100.0));
    });
    connect(m_runner, &PipelineRunner::logMessage, this, [this](const QString& t) {
        m_log->appendPlainText(t);
    });
    connect(m_runner, &PipelineRunner::finished, this, [this](bool ok, const QString& err) {
        m_runAction->setText(tr("Run"));
        m_runAction->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start"),
                                              style()->standardIcon(QStyle::SP_MediaPlay)));
        m_progress->setValue(ok ? 100 : 0);
        statusBar()->showMessage(ok ? tr("Finished") : tr("Stopped"), 4000);
        if (!ok && !err.isEmpty())
            m_log->appendPlainText(tr("✗ %1").arg(err));
        else if (ok)
            m_log->appendPlainText(tr("✓ Conversion complete."));
    });

    connect(m_preview, &MediaPreview::started, this, [this] {
        m_previewAction->setText(tr("Stop Preview"));
        statusBar()->showMessage(tr("Previewing input…"));
    });
    connect(m_preview, &MediaPreview::finished, this, [this] {
        m_previewAction->setText(tr("Preview Input"));
    });
    connect(m_preview, &MediaPreview::logMessage, this,
            [this](const QString& t) { m_log->appendPlainText(t); });

    connect(m_scene, &NodeEditorScene::selectedNodeChanged, this, [this](Node* node) {
        m_properties->setNode(node);
        m_info->setNode(node);
    });
    connect(m_scene, &NodeEditorScene::nodeModified, this, [this](Node* node) {
        m_properties->onNodeModified(node);
        m_info->onNodeModified(node);
    });

    statusBar()->showMessage(tr("Ready"));
    resize(1200, 760);
    updateWindowTitle();
}

MainWindow::~MainWindow() = default;

void MainWindow::createActions()
{
    // Theme icon with a QStyle fallback for environments without a full theme.
    auto icon = [this](const char* themeName, QStyle::StandardPixmap fallback) {
        QIcon i = QIcon::fromTheme(QString::fromLatin1(themeName));
        return i.isNull() ? style()->standardIcon(fallback) : i;
    };
    auto themeOnly = [](const char* themeName) {
        return QIcon::fromTheme(QString::fromLatin1(themeName));
    };

    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    // --- File ---------------------------------------------------------------
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    auto* newAct = fileMenu->addAction(icon("document-new", QStyle::SP_FileIcon), tr("&New"));
    newAct->setShortcut(QKeySequence::New);
    connect(newAct, &QAction::triggered, this, &MainWindow::newGraph);

    auto* openAct = fileMenu->addAction(icon("document-open", QStyle::SP_DirOpenIcon), tr("&Open…"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openGraph);

    auto* saveAct = fileMenu->addAction(icon("document-save", QStyle::SP_DialogSaveButton), tr("&Save"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MainWindow::saveGraph);

    auto* saveAsAct = fileMenu->addAction(tr("Save &As…"));
    saveAsAct->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::saveGraphAs);

    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("&Quit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    // --- Edit ---------------------------------------------------------------
    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    auto* deleteAct = editMenu->addAction(tr("&Delete Selection"));
    deleteAct->setShortcut(QKeySequence::Delete);
    connect(deleteAct, &QAction::triggered, this, [this] { m_scene->deleteSelection(); });

    editMenu->addSeparator();
    auto* findAct = editMenu->addAction(tr("&Find Element…"));
    findAct->setShortcut(QKeySequence::Find);
    connect(findAct, &QAction::triggered, this, [this] {
        if (m_palette) m_palette->focusSearch();
    });

    // --- View ---------------------------------------------------------------
    auto* viewMenu = menuBar()->addMenu(tr("&View"));

    auto* zoomInAct = viewMenu->addAction(themeOnly("zoom-in"), tr("Zoom &In"));
    zoomInAct->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAct, &QAction::triggered, m_view, &NodeEditorView::zoomIn);

    auto* zoomOutAct = viewMenu->addAction(themeOnly("zoom-out"), tr("Zoom &Out"));
    zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAct, &QAction::triggered, m_view, &NodeEditorView::zoomOut);

    auto* fitAct = viewMenu->addAction(themeOnly("zoom-fit-best"), tr("&Fit to Window"));
    fitAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    connect(fitAct, &QAction::triggered, m_view, &NodeEditorView::fitContent);

    auto* zoomResetAct = viewMenu->addAction(themeOnly("zoom-original"), tr("&Reset Zoom"));
    connect(zoomResetAct, &QAction::triggered, m_view, &NodeEditorView::resetZoom);

    // --- Pipeline -----------------------------------------------------------
    auto* runMenu = menuBar()->addMenu(tr("&Pipeline"));

    m_runAction = runMenu->addAction(icon("media-playback-start", QStyle::SP_MediaPlay), tr("&Run"));
    m_runAction->setShortcut(Qt::Key_F5);
    m_runAction->setEnabled(PipelineRunner::available());
    if (!PipelineRunner::available())
        m_runAction->setToolTip(tr("Built without the GStreamer backend."));
    connect(m_runAction, &QAction::triggered, this, &MainWindow::toggleRun);

    m_previewAction = runMenu->addAction(icon("view-preview", QStyle::SP_MediaPlay), tr("&Preview Input"));
    m_previewAction->setShortcut(Qt::Key_F6);
    m_previewAction->setEnabled(MediaPreview::available());
    m_previewAction->setToolTip(tr("Play the first File Source's input file"));
    connect(m_previewAction, &QAction::triggered, this, &MainWindow::togglePreview);

    // --- Toolbar: grouped by category ---------------------------------------
    toolbar->addAction(newAct);
    toolbar->addAction(openAct);
    toolbar->addAction(saveAct);
    toolbar->addSeparator();
    toolbar->addAction(zoomInAct);
    toolbar->addAction(zoomOutAct);
    toolbar->addAction(fitAct);
    toolbar->addAction(zoomResetAct);
    toolbar->addSeparator();
    toolbar->addAction(m_runAction);
    toolbar->addAction(m_previewAction);
}

void MainWindow::createDocks()
{
    auto* paletteDock = new QDockWidget(tr("Nodes"), this);
    paletteDock->setObjectName(QStringLiteral("paletteDock"));
    m_palette = new NodePalette(paletteDock);
    paletteDock->setWidget(m_palette);
    addDockWidget(Qt::LeftDockWidgetArea, paletteDock);
    connect(m_palette, &NodePalette::nodeRequested, this, &MainWindow::addNode);

    auto* templatesDock = new QDockWidget(tr("Templates"), this);
    templatesDock->setObjectName(QStringLiteral("templatesDock"));
    m_templates = new TemplatePanel(templatesDock);
    templatesDock->setWidget(m_templates);
    addDockWidget(Qt::LeftDockWidgetArea, templatesDock);

    connect(m_templates, &TemplatePanel::loadBuiltinRequested, this, [this](const QString& id) {
        if (!confirmReplacePipeline())
            return;
        m_graph->clear();
        m_currentFile.clear();
        QString error;
        if (!TemplateLibrary::buildBuiltin(id, *m_graph, &error))
            QMessageBox::warning(this, tr("Template"), error);
        updateWindowTitle();
        m_view->resetZoom();
    });
    connect(m_templates, &TemplatePanel::loadUserRequested, this, [this](const QString& path) {
        if (!confirmReplacePipeline())
            return;
        QString error;
        if (!GraphSerializer::loadFromFile(*m_graph, path, &error)) {
            QMessageBox::warning(this, tr("Template"), error);
            return;
        }
        m_currentFile.clear();   // a template is a starting point, not the file
        updateWindowTitle();
        m_view->resetZoom();
    });
    connect(m_templates, &TemplatePanel::saveCurrentRequested, this, [this] {
        const QString name = QInputDialog::getText(this, tr("Save Template"),
                                                   tr("Template name:"));
        if (name.trimmed().isEmpty())
            return;
        QString error;
        if (TemplateLibrary::saveUserTemplate(*m_graph, name, &error).isEmpty())
            QMessageBox::warning(this, tr("Save Template"), error);
        else
            m_templates->refresh();
    });
    connect(m_templates, &TemplatePanel::deleteUserRequested, this, [this](const QString& path) {
        if (QMessageBox::question(this, tr("Delete Template"),
                tr("Delete this template?")) != QMessageBox::Yes)
            return;
        QFile::remove(path);
        m_templates->refresh();
    });

    // Stack the palette and templates as tabs on the left, palette shown first.
    tabifyDockWidget(paletteDock, templatesDock);
    paletteDock->raise();

    auto* propsDock = new QDockWidget(tr("Properties"), this);
    propsDock->setObjectName(QStringLiteral("propertiesDock"));
    m_properties = new PropertiesPanel(propsDock);
    m_properties->setGraph(m_graph);
    propsDock->setWidget(m_properties);
    addDockWidget(Qt::RightDockWidgetArea, propsDock);

    auto* infoDock = new QDockWidget(tr("Info"), this);
    infoDock->setObjectName(QStringLiteral("infoDock"));
    m_info = new InfoPanel(infoDock);
    infoDock->setWidget(m_info);
    addDockWidget(Qt::RightDockWidgetArea, infoDock);

    // Stack Properties and Info as tabs in the right area, Info shown first.
    tabifyDockWidget(propsDock, infoDock);
    infoDock->raise();
}

void MainWindow::createPipelineDock()
{
    auto* dock = new QDockWidget(tr("Pipeline"), this);
    dock->setObjectName(QStringLiteral("pipelineDock"));

    auto* host = new QWidget(dock);
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(8, 8, 8, 8);

    m_progress = new QProgressBar(host);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    layout->addWidget(m_progress);

    m_log = new QPlainTextEdit(host);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(1000);
    m_log->setPlaceholderText(tr("Pipeline output will appear here."));
    layout->addWidget(m_log);

    dock->setWidget(host);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
}

bool MainWindow::confirmReplacePipeline()
{
    if (m_graph->nodes().empty())
        return true;
    return QMessageBox::question(this, tr("Load Template"),
               tr("Replace the current pipeline?")) == QMessageBox::Yes;
}

void MainWindow::toggleRun()
{
    if (m_runner->isRunning()) {
        m_runner->stop();
        return;
    }

    m_log->clear();
    QString error;
    if (!m_runner->run(*m_graph, &error)) {
        m_log->appendPlainText(tr("✗ %1").arg(error));
        QMessageBox::warning(this, tr("Cannot run"), error);
    }
}

void MainWindow::togglePreview()
{
    if (m_preview->isPlaying()) {
        m_preview->stop();
        return;
    }

    // Preview the input of the first File Source that has a file assigned.
    QString path;
    for (const auto& node : m_graph->nodes()) {
        if (node->typeId == QLatin1String("source.file")) {
            const QString loc = node->properties.value(QStringLiteral("location")).toString();
            if (!loc.isEmpty()) {
                path = loc;
                break;
            }
        }
    }
    if (path.isEmpty()) {
        statusBar()->showMessage(tr("No input file to preview."), 4000);
        return;
    }

    QString error;
    if (!m_preview->play(path, &error))
        QMessageBox::warning(this, tr("Preview"), error);
}

QPointF MainWindow::viewportCenterInScene() const
{
    return m_view->mapToScene(m_view->viewport()->rect().center());
}

void MainWindow::addNode(const QString& typeId)
{
    // Cascade newly added nodes so they don't stack exactly on top of each other.
    const QPointF base = viewportCenterInScene();
    const qreal offset = static_cast<qreal>(m_addCounter % 6) * 26.0;
    m_addCounter++;

    Node* node = m_graph->addNode(typeId, base + QPointF(offset - 80.0, offset - 40.0));
    if (node)
        statusBar()->showMessage(tr("Added “%1”").arg(node->title), 2000);
}

void MainWindow::newGraph()
{
    m_graph->clear();
    m_currentFile.clear();
    m_addCounter = 0;
    updateWindowTitle();
    statusBar()->showMessage(tr("New pipeline"), 2000);
}

void MainWindow::openGraph()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Pipeline"), QString(),
        tr("avena pipeline (*.abk);;All files (*)"));
    if (path.isEmpty())
        return;

    QString error;
    if (!GraphSerializer::loadFromFile(*m_graph, path, &error)) {
        QMessageBox::warning(this, tr("Open failed"), error);
        return;
    }
    m_currentFile = path;
    updateWindowTitle();
    statusBar()->showMessage(tr("Opened %1").arg(path), 3000);
}

bool MainWindow::saveGraph()
{
    if (m_currentFile.isEmpty())
        return saveGraphAs();

    QString error;
    if (!GraphSerializer::saveToFile(*m_graph, m_currentFile, &error)) {
        QMessageBox::warning(this, tr("Save failed"), error);
        return false;
    }
    statusBar()->showMessage(tr("Saved %1").arg(m_currentFile), 3000);
    return true;
}

bool MainWindow::saveGraphAs()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Pipeline"), QString(),
        tr("avena pipeline (*.abk);;All files (*)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QLatin1String(".abk")))
        path += QLatin1String(".abk");

    m_currentFile = path;
    if (!saveGraph())
        return false;
    updateWindowTitle();
    return true;
}

void MainWindow::updateWindowTitle()
{
    const QString name = m_currentFile.isEmpty()
        ? tr("Untitled")
        : QFileInfo(m_currentFile).fileName();
    setWindowTitle(tr("%1 — avena").arg(name));
}

} // namespace ab
