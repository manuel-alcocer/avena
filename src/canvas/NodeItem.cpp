#include "canvas/NodeItem.h"

#include "canvas/PortItem.h"
#include "graph/Node.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <algorithm>

namespace ab {

NodeItem::NodeItem(Node* node) : m_node(node)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setZValue(1.0);
    setPos(m_node->position);

    for (int i = 0; i < static_cast<int>(m_node->inputs.size()); ++i) {
        const Port& p = m_node->inputs[static_cast<std::size_t>(i)];
        m_inputItems.push_back(new PortItem(
            this, PortRef{m_node->id, PortDirection::Input, i}, p.mediaType, p.name));
    }
    for (int i = 0; i < static_cast<int>(m_node->outputs.size()); ++i) {
        const Port& p = m_node->outputs[static_cast<std::size_t>(i)];
        m_outputItems.push_back(new PortItem(
            this, PortRef{m_node->id, PortDirection::Output, i}, p.mediaType, p.name));
    }

    layoutPorts();
}

int NodeItem::nodeId() const
{
    return m_node->id;
}

qreal NodeItem::nodeWidth() const
{
    return m_node->width > 0.0 ? m_node->width : kWidth;
}

QRectF NodeItem::resizeHandleRect() const
{
    const qreal w = nodeWidth();
    const qreal h = bodyHeight();
    return QRectF(w - kHandleSize, h - kHandleSize, kHandleSize, kHandleSize);
}

qreal NodeItem::portsAreaHeight() const
{
    const int rows = std::max<int>({1,
        static_cast<int>(m_inputItems.size()),
        static_cast<int>(m_outputItems.size())});
    return kHeaderH + kPortMargin * 2.0 + (rows - 1) * kPortSpacing;
}

qreal NodeItem::bodyHeight() const
{
    qreal h = portsAreaHeight();
    if (hasFileField())
        h += kFileRowH;
    return h;
}

bool NodeItem::hasFileField() const
{
    return m_node->properties.contains(QStringLiteral("location"));
}

QRectF NodeItem::fileRowRect() const
{
    const qreal top = portsAreaHeight() - 4.0;
    return QRectF(10.0, top, nodeWidth() - 20.0, kFileRowH - 12.0);
}

QRectF NodeItem::boundingRect() const
{
    // Extra margin so port discs and the selection glow are not clipped.
    constexpr qreal m = PortItem::kRadius + 3.0;
    return QRectF(-m, -m, nodeWidth() + 2 * m, bodyHeight() + 2 * m);
}

void NodeItem::layoutPorts()
{
    auto place = [&](std::vector<PortItem*>& items, qreal x) {
        for (std::size_t i = 0; i < items.size(); ++i) {
            const qreal y = kHeaderH + kPortMargin + static_cast<qreal>(i) * kPortSpacing;
            items[i]->setPos(x, y);
        }
    };
    place(m_inputItems, 0.0);
    place(m_outputItems, nodeWidth());
}

PortItem* NodeItem::portItem(PortDirection dir, int index) const
{
    const auto& items = dir == PortDirection::Input ? m_inputItems : m_outputItems;
    if (index < 0 || index >= static_cast<int>(items.size()))
        return nullptr;
    return items[static_cast<std::size_t>(index)];
}

void NodeItem::syncFromModel()
{
    if (pos() != m_node->position)
        setPos(m_node->position);
    update();
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                     QWidget*)
{
    const qreal width = nodeWidth();
    const QRectF body(0, 0, width, bodyHeight());
    const bool selected = option->state & QStyle::State_Selected;

    painter->setRenderHint(QPainter::Antialiasing, true);

    // Card body.
    QPainterPath path;
    path.addRoundedRect(body, kCornerR, kCornerR);
    painter->setPen(QPen(selected ? QColor(0xFF, 0xD5, 0x4F)
                                   : QColor(0x20, 0x24, 0x2A), selected ? 2.0 : 1.0));
    painter->setBrush(QColor(0x2E, 0x34, 0x3C));
    painter->drawPath(path);

    // Header band (clipped to the rounded top).
    painter->save();
    painter->setClipPath(path);
    QRectF header(0, 0, width, kHeaderH);
    painter->fillRect(header, m_node->color);
    painter->restore();

    // Title.
    painter->setPen(Qt::white);
    QFont f = painter->font();
    f.setBold(true);
    painter->setFont(f);
    const QFontMetricsF fm(f);
    const QString title = fm.elidedText(m_node->title, Qt::ElideRight, width - 16.0);
    painter->drawText(header.adjusted(10, 0, -6, 0),
                      Qt::AlignVCenter | Qt::AlignLeft, title);

    // Port labels.
    painter->setPen(QColor(0xCF, 0xD8, 0xDC));
    QFont lf = painter->font();
    lf.setBold(false);
    lf.setPointSizeF(std::max(7.0, lf.pointSizeF() - 1.0));
    painter->setFont(lf);

    const qreal labelW = width / 2.0 - 10.0;
    for (PortItem* p : m_inputItems) {
        const QPointF c = p->pos();
        painter->drawText(QRectF(c.x() + 10, c.y() - 9, labelW, 18),
                          Qt::AlignVCenter | Qt::AlignLeft, p->label());
    }
    for (PortItem* p : m_outputItems) {
        const QPointF c = p->pos();
        painter->drawText(QRectF(c.x() - labelW, c.y() - 9, labelW, 18),
                          Qt::AlignVCenter | Qt::AlignRight, p->label());
    }

    // File field (sources/sinks with a `location` property).
    if (hasFileField()) {
        const QRectF row = fileRowRect();

        painter->setPen(QPen(m_dropHighlight ? QColor(0xFF, 0xD5, 0x4F)
                                             : QColor(0x3A, 0x41, 0x50),
                             m_dropHighlight ? 1.6 : 1.0));
        painter->setBrush(QColor(0x18, 0x1B, 0x20));
        painter->drawRoundedRect(row, 4, 4);

        // Browse button on the right edge.
        const QRectF browse(row.right() - row.height(), row.top(),
                            row.height(), row.height());
        painter->setPen(QPen(QColor(0x4A, 0x52, 0x60), 1.0));
        painter->setBrush(QColor(0x2B, 0x30, 0x38));
        painter->drawRoundedRect(browse.adjusted(2, 2, -2, -2), 3, 3);
        painter->setPen(QColor(0xCF, 0xD8, 0xDC));
        painter->drawText(browse, Qt::AlignCenter, QStringLiteral("…"));

        // File name or placeholder.
        const QString location = m_node->properties.value(
            QStringLiteral("location")).toString();
        const bool empty = location.isEmpty();
        const QString text = empty ? tr("Drop a file or browse…")
                                   : QFileInfo(location).fileName();
        const QRectF textRect = row.adjusted(8, 0, -(browse.width() + 4), 0);
        painter->setPen(empty ? QColor(0x80, 0x88, 0x90) : QColor(0xE6, 0xEA, 0xEE));
        const QString elided = QFontMetricsF(lf).elidedText(
            text, Qt::ElideMiddle, textRect.width());
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elided);
    }

    // Resize handle: three diagonal grip lines in the bottom-right corner.
    const QRectF handle = resizeHandleRect();
    painter->setPen(QPen(QColor(selected ? 0xFF : 0x6A,
                                selected ? 0xD5 : 0x72,
                                selected ? 0x4F : 0x80), 1.2));
    for (int i = 1; i <= 3; ++i) {
        const qreal off = i * (kHandleSize / 4.0);
        painter->drawLine(QPointF(handle.right() - off, handle.bottom()),
                          QPointF(handle.right(), handle.bottom() - off));
    }
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionHasChanged) {
        m_node->position = pos();
        emit moved();
    }
    return QGraphicsObject::itemChange(change, value);
}

void NodeItem::setDropHighlight(bool on)
{
    if (m_dropHighlight == on)
        return;
    m_dropHighlight = on;
    update();
}

void NodeItem::openFileBrowser()
{
    QWidget* parent = nullptr;
    if (scene() && !scene()->views().isEmpty())
        parent = scene()->views().first();

    const QString current = m_node->properties.value(
        QStringLiteral("location")).toString();
    const QString dir = current.isEmpty() ? QDir::homePath()
                                          : QFileInfo(current).absolutePath();

    QString path;
    if (m_node->typeId.startsWith(QLatin1String("sink."))) {
        path = QFileDialog::getSaveFileName(parent, tr("Choose output file"), dir);
    } else {
        path = QFileDialog::getOpenFileName(
            parent, tr("Choose input file"), dir,
            tr("Media files (*.mp4 *.mkv *.mov *.avi *.webm *.m4v *.ts *.flv "
               "*.mp3 *.aac *.wav *.flac *.ogg *.opus);;All files (*)"));
    }
    if (!path.isEmpty())
        emit fileChosen(path);
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton &&
        resizeHandleRect().contains(event->pos())) {
        m_resizing = true;
        m_resizeStartWidth = nodeWidth();
        m_resizeStartX = event->scenePos().x();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && hasFileField() &&
        fileRowRect().contains(event->pos())) {
        m_pressInFileRow = true;
        event->accept();
        return;
    }
    QGraphicsObject::mousePressEvent(event);
}

void NodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_resizing) {
        const qreal delta = event->scenePos().x() - m_resizeStartX;
        const qreal w = std::clamp(m_resizeStartWidth + delta, kMinWidth, kMaxWidth);
        if (w != nodeWidth()) {
            prepareGeometryChange();
            m_node->width = w;
            layoutPorts();
            update();
            emit moved();   // output ports moved: re-route their connections
        }
        event->accept();
        return;
    }
    QGraphicsObject::mouseMoveEvent(event);
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_resizing) {
        m_resizing = false;
        event->accept();
        return;
    }
    if (m_pressInFileRow) {
        m_pressInFileRow = false;
        if (fileRowRect().contains(event->pos()))
            openFileBrowser();
        event->accept();
        return;
    }
    QGraphicsObject::mouseReleaseEvent(event);
}

void NodeItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    if (resizeHandleRect().contains(event->pos()))
        setCursor(Qt::SizeFDiagCursor);
    else if (hasFileField() && fileRowRect().contains(event->pos()))
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();
    QGraphicsObject::hoverMoveEvent(event);
}

} // namespace ab
