#pragma once

#include <QGraphicsObject>

namespace ab {

class PortItem;

/// A bezier edge between an output port and an input port. Also used in a
/// transient "dragging" state where the target end follows the cursor
/// (`to == nullptr`, endpoint set via setFreeEnd).
class ConnectionItem : public QGraphicsObject {
    Q_OBJECT

public:
    enum { Type = UserType + 3 };
    int type() const override { return Type; }

    /// Established connection.
    ConnectionItem(int connectionId, PortItem* from, PortItem* to);
    /// Transient connection being dragged from `from`.
    explicit ConnectionItem(PortItem* from);

    [[nodiscard]] int connectionId() const { return m_connectionId; }
    [[nodiscard]] bool isTransient() const { return m_to == nullptr; }

    void setFreeEnd(QPointF scenePos);
    void updatePath();

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    QPointF sourcePoint() const;
    QPointF targetPoint() const;
    QPainterPath buildPath() const;

    int       m_connectionId = -1;
    PortItem* m_from = nullptr;
    PortItem* m_to = nullptr;
    QPointF   m_freeEnd;
    QPainterPath m_path;
};

} // namespace ab
