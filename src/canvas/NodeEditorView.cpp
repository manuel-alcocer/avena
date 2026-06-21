#include "canvas/NodeEditorView.h"

#include "canvas/NodeEditorScene.h"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <cmath>

namespace ab {

NodeEditorView::NodeEditorView(NodeEditorScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent), m_scene(scene)
{
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // Full updates avoid stale "ghost" edges left behind when connections move
    // or are deleted (Smart/BoundingRect modes can leave artifacts).
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setAcceptDrops(true);   // forwards file drops to the scene
}

void NodeEditorView::scaleBy(qreal factor)
{
    const qreal next = m_zoom * factor;
    if (next < 0.2 || next > 4.0)
        return;
    m_zoom = next;
    scale(factor, factor);
}

void NodeEditorView::resetZoom()
{
    if (m_zoom != 0.0)
        scale(1.0 / m_zoom, 1.0 / m_zoom);
    m_zoom = 1.0;
}

void NodeEditorView::zoomIn()  { scaleBy(1.2); }
void NodeEditorView::zoomOut() { scaleBy(1.0 / 1.2); }

void NodeEditorView::fitContent()
{
    if (!m_scene)
        return;
    QRectF rect = m_scene->itemsBoundingRect();
    if (rect.isEmpty())
        return;
    rect.adjust(-48, -48, 48, 48);   // breathing room around the graph
    fitInView(rect, Qt::KeepAspectRatio);
    m_zoom = transform().m11();       // keep wheel-zoom clamping in sync
}

void NodeEditorView::wheelEvent(QWheelEvent* event)
{
    const qreal step = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    scaleBy(step);
    event->accept();
}

void NodeEditorView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (m_scene)
            m_scene->deleteSelection();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void NodeEditorView::mousePressEvent(QMouseEvent* event)
{
    // Pan with the middle button, or with Ctrl + right button.
    const bool ctrlRight = event->button() == Qt::RightButton &&
                           (event->modifiers() & Qt::ControlModifier);
    if (event->button() == Qt::MiddleButton || ctrlRight) {
        m_panning = true;
        m_lastPanPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void NodeEditorView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPanPos;
        m_lastPanPos = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void NodeEditorView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_panning && (event->button() == Qt::MiddleButton ||
                      event->button() == Qt::RightButton)) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void NodeEditorView::contextMenuEvent(QContextMenuEvent* event)
{
    // Ctrl + right button is reserved for panning — suppress the menu then.
    if (event->modifiers() & Qt::ControlModifier) {
        event->accept();
        return;
    }
    QGraphicsView::contextMenuEvent(event);
}

void NodeEditorView::drawBackground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawBackground(painter, rect);

    // Pan handling lives here so it tracks with mouse moves; the move event is
    // delivered to the viewport, so we read the global state via the panning
    // flag set in mousePress/Release.
    constexpr qreal kGrid = 24.0;

    const qreal left   = std::floor(rect.left()   / kGrid) * kGrid;
    const qreal top    = std::floor(rect.top()    / kGrid) * kGrid;

    QPen minorPen(QColor(0x26, 0x2B, 0x32));
    minorPen.setWidthF(0.0);
    painter->setPen(minorPen);

    for (qreal x = left; x < rect.right(); x += kGrid)
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    for (qreal y = top; y < rect.bottom(); y += kGrid)
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));

    // Stronger lines every 5 cells.
    QPen majorPen(QColor(0x31, 0x37, 0x40));
    majorPen.setWidthF(0.0);
    painter->setPen(majorPen);

    const qreal major = kGrid * 5.0;
    const qreal mleft = std::floor(rect.left() / major) * major;
    const qreal mtop  = std::floor(rect.top()  / major) * major;
    for (qreal x = mleft; x < rect.right(); x += major)
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    for (qreal y = mtop; y < rect.bottom(); y += major)
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
}

} // namespace ab
