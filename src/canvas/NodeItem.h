#pragma once

#include "graph/Port.h"

#include <QGraphicsObject>

#include <vector>

namespace ab {

class Node;
class PortItem;

/// The visual representation of a single graph Node: a rounded card with a
/// coloured header and input/output ports down the sides. Movable and
/// selectable. Position changes are written back to the model and broadcast so
/// the scene can re-route connected edges.
class NodeItem : public QGraphicsObject {
    Q_OBJECT

public:
    enum { Type = UserType + 1 };
    int type() const override { return Type; }

    explicit NodeItem(Node* node);

    [[nodiscard]] Node* node() const { return m_node; }
    [[nodiscard]] int   nodeId() const;

    QRectF boundingRect() const override;
    void   paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                 QWidget* widget) override;

    /// Returns the PortItem for a given direction/index, or nullptr.
    PortItem* portItem(PortDirection dir, int index) const;

    /// Re-reads title/position from the model.
    void syncFromModel();

    /// True if this node exposes a file path (`location` property), in which
    /// case an in-card file field with a browse button and drop support is
    /// drawn below the ports.
    [[nodiscard]] bool hasFileField() const;

    /// Toggles the "drop here" highlight (driven by the scene during a drag).
    void setDropHighlight(bool on);

    // Layout metrics (scene units).
    static constexpr qreal kWidth       = 168.0;  ///< Default (auto) width.
    static constexpr qreal kMinWidth    = 140.0;
    static constexpr qreal kMaxWidth    = 460.0;
    static constexpr qreal kHeaderH     = 28.0;
    static constexpr qreal kPortSpacing = 24.0;
    static constexpr qreal kPortMargin  = 18.0;
    static constexpr qreal kCornerR     = 8.0;
    static constexpr qreal kFileRowH    = 32.0;
    static constexpr qreal kHandleSize  = 14.0;

signals:
    void moved();
    /// The user picked a file via the in-card browse button.
    void fileChosen(const QString& path);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    qreal  nodeWidth() const;
    qreal  portsAreaHeight() const;
    qreal  bodyHeight() const;
    void   layoutPorts();
    QRectF fileRowRect() const;
    QRectF resizeHandleRect() const;
    void   openFileBrowser();

    Node* m_node;
    std::vector<PortItem*> m_inputItems;
    std::vector<PortItem*> m_outputItems;
    bool  m_pressInFileRow = false;
    bool  m_dropHighlight = false;
    bool  m_resizing = false;
    qreal m_resizeStartWidth = 0.0;
    qreal m_resizeStartX = 0.0;
};

} // namespace ab
