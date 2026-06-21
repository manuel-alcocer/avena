#pragma once

#include <QMainWindow>
#include <QString>

class QAction;
class QPlainTextEdit;
class QProgressBar;

namespace ab {

class Graph;
class NodeEditorScene;
class NodeEditorView;
class NodePalette;
class PropertiesPanel;
class InfoPanel;
class TemplatePanel;
class PipelineRunner;
class MediaPreview;

/// Top-level window: hosts the node editor canvas with a node palette on the
/// left and a properties panel on the right, plus file/edit/view actions.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void newGraph();
    void openGraph();
    bool saveGraph();
    bool saveGraphAs();
    void addNode(const QString& typeId);
    void toggleRun();
    void togglePreview();

private:
    void createActions();
    void createDocks();
    void createPipelineDock();
    void updateWindowTitle();
    bool confirmReplacePipeline();
    QPointF viewportCenterInScene() const;

    Graph*           m_graph = nullptr;
    NodeEditorScene* m_scene = nullptr;
    NodeEditorView*  m_view = nullptr;
    NodePalette*     m_palette = nullptr;
    PropertiesPanel* m_properties = nullptr;
    InfoPanel*       m_info = nullptr;
    TemplatePanel*   m_templates = nullptr;

    PipelineRunner*  m_runner = nullptr;
    MediaPreview*    m_preview = nullptr;
    QAction*         m_runAction = nullptr;
    QAction*         m_previewAction = nullptr;
    QProgressBar*    m_progress = nullptr;
    QPlainTextEdit*  m_log = nullptr;

    QString m_currentFile;
    int     m_addCounter = 0;
};

} // namespace ab
