#pragma once

#include <QMainWindow>
#include <QString>

class QAction;
class QDockWidget;
class QPlainTextEdit;
class QProgressBar;
class QShowEvent;

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

/// Top-level window: hosts the node editor canvas (centre) with the node
/// palette/templates on the left and a bottom panel that tabs the pipeline
/// output, properties and info together, plus file/edit/view actions.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void showEvent(QShowEvent* event) override;

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
    /// Enables the Properties/Info bottom tabs when a node is selected; when
    /// none is, disables them and falls back to the Pipeline tab.
    void setSidePanelsActive(bool active);
    /// Applies the current enabled state to the Properties/Info dock tabs. The
    /// dock QTabBar only exists after the window is laid out, so this is also
    /// run from showEvent.
    void applyPanelTabEnabled();
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
    QDockWidget*     m_pipelineDock = nullptr;
    QDockWidget*     m_propertiesDock = nullptr;
    QDockWidget*     m_infoDock = nullptr;
    bool             m_sidePanelsActive = false;

    QString m_currentFile;
    int     m_addCounter = 0;
};

} // namespace ab
