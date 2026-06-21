#include "canvas/PortItem.h"

#include "canvas/NodeItem.h"

#include <QPainter>

namespace ab {

PortItem::PortItem(NodeItem* parentNode, PortRef ref, MediaType mediaType,
                   QString label)
    : QGraphicsItem(parentNode)
    , m_ref(ref)
    , m_mediaType(mediaType)
    , m_label(std::move(label))
{
    setAcceptHoverEvents(true);
    setToolTip(m_label);
}

QRectF PortItem::boundingRect() const
{
    // A little padding around the disc for the hover ring.
    const qreal r = kRadius + 2.0;
    return QRectF(-r, -r, 2 * r, 2 * r);
}

void PortItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const QColor base = mediaTypeColor(m_mediaType);

    painter->setRenderHint(QPainter::Antialiasing, true);

    if (m_hovered || m_highlighted) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(base.red(), base.green(), base.blue(), 90));
        painter->drawEllipse(QPointF(0, 0), kRadius + 3.0, kRadius + 3.0);
    }

    painter->setPen(QPen(base.darker(140), 1.5));
    painter->setBrush(m_highlighted ? base.lighter(120) : base);
    painter->drawEllipse(QPointF(0, 0), kRadius, kRadius);
}

QPointF PortItem::sceneAnchor() const
{
    return mapToScene(QPointF(0, 0));
}

void PortItem::setHighlighted(bool on)
{
    if (m_highlighted == on)
        return;
    m_highlighted = on;
    update();
}

void PortItem::hoverEnterEvent(QGraphicsSceneHoverEvent*)
{
    m_hovered = true;
    update();
}

void PortItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
{
    m_hovered = false;
    update();
}

} // namespace ab
