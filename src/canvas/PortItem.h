#pragma once

#include "graph/Port.h"

#include <QGraphicsItem>

namespace ab {

class NodeItem;

/// A small circular connection point drawn on a NodeItem. Purely visual;
/// connection dragging is orchestrated by NodeEditorScene.
class PortItem : public QGraphicsItem {
public:
    enum { Type = UserType + 2 };
    int type() const override { return Type; }

    PortItem(NodeItem* parentNode, PortRef ref, MediaType mediaType,
             QString label);

    QRectF boundingRect() const override;
    void   paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                 QWidget* widget) override;

    [[nodiscard]] const PortRef& portRef() const { return m_ref; }
    [[nodiscard]] MediaType      mediaType() const { return m_mediaType; }
    [[nodiscard]] const QString& label() const { return m_label; }

    /// Centre of the port in scene coordinates (connection anchor point).
    [[nodiscard]] QPointF sceneAnchor() const;

    void setHighlighted(bool on);

    static constexpr qreal kRadius = 6.0;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    PortRef   m_ref;
    MediaType m_mediaType;
    QString   m_label;
    bool      m_hovered = false;
    bool      m_highlighted = false;
};

} // namespace ab
