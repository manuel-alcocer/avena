#pragma once

#include <QGraphicsView>

namespace ab {

class NodeEditorScene;

/// QGraphicsView wrapper for the node editor: wheel zoom, middle-button pan,
/// a dotted background grid and Delete-to-remove.
class NodeEditorView : public QGraphicsView {
    Q_OBJECT

public:
    explicit NodeEditorView(NodeEditorScene* scene, QWidget* parent = nullptr);

    void resetZoom();
    void zoomIn();
    void zoomOut();
    /// Centers and zooms so the whole pipeline fits in the viewport.
    void fitContent();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    void scaleBy(qreal factor);

    NodeEditorScene* m_scene = nullptr;
    qreal m_zoom = 1.0;
    bool  m_panning = false;
    QPoint m_lastPanPos;
};

} // namespace ab
