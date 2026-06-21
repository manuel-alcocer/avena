#include "canvas/ConnectionItem.h"

#include "canvas/PortItem.h"

#include <QPainter>
#include <QPainterPathStroker>

#include <algorithm>
#include <cmath>

namespace ab {

ConnectionItem::ConnectionItem(int connectionId, PortItem* from, PortItem* to)
    : m_connectionId(connectionId), m_from(from), m_to(to)
{
    setZValue(0.0);
    setFlag(ItemIsSelectable, true);
    updatePath();
}

ConnectionItem::ConnectionItem(PortItem* from)
    : m_from(from), m_freeEnd(from ? from->sceneAnchor() : QPointF())
{
    setZValue(2.0); // float above nodes while dragging
    updatePath();
}

void ConnectionItem::setFreeEnd(QPointF scenePos)
{
    m_freeEnd = scenePos;
    updatePath();
}

QPointF ConnectionItem::sourcePoint() const
{
    return m_from ? m_from->sceneAnchor() : QPointF();
}

QPointF ConnectionItem::targetPoint() const
{
    return m_to ? m_to->sceneAnchor() : m_freeEnd;
}

QPainterPath ConnectionItem::buildPath() const
{
    const QPointF s = sourcePoint();
    const QPointF t = targetPoint();

    // Horizontal tangents; control offset scales with horizontal distance so
    // close ports still bow nicely.
    const qreal dx = std::abs(t.x() - s.x());
    const qreal offset = std::clamp(dx * 0.5, 30.0, 160.0);

    QPainterPath p(s);
    p.cubicTo(s + QPointF(offset, 0), t - QPointF(offset, 0), t);
    return p;
}

void ConnectionItem::updatePath()
{
    prepareGeometryChange();
    m_path = buildPath();
    update();
}

QRectF ConnectionItem::boundingRect() const
{
    return m_path.boundingRect().adjusted(-4, -4, 4, 4);
}

QPainterPath ConnectionItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(10.0);
    return stroker.createStroke(m_path);
}

void ConnectionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing, true);

    QColor color = m_from ? mediaTypeColor(m_from->mediaType())
                          : QColor(0xB0, 0xBE, 0xC5);

    if (isSelected())
        color = QColor(0xFF, 0xD5, 0x4F);

    QPen pen(color, 2.4);
    pen.setCapStyle(Qt::RoundCap);
    // Value (parameter) edges are drawn dashed to distinguish them from media.
    const bool valueEdge = m_from && m_from->mediaType() == MediaType::Value;
    if (isTransient() || valueEdge)
        pen.setStyle(Qt::DashLine);
    if (valueEdge)
        pen.setWidthF(1.8);

    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(m_path);
}

} // namespace ab
