#include "canvas/NodeEditorScene.h"

#include "canvas/ConnectionItem.h"
#include "canvas/DragTypes.h"
#include "canvas/NodeItem.h"
#include "canvas/PortItem.h"
#include "graph/Connection.h"
#include "graph/Graph.h"
#include "graph/Node.h"
#include "media/ElementProperties.h"

#include <QAction>
#include <QCursor>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QMenu>
#include <QMimeData>
#include <QStringList>
#include <QTransform>
#include <QUrl>

namespace ab {

NodeEditorScene::NodeEditorScene(QObject* parent) : QGraphicsScene(parent)
{
    setBackgroundBrush(QColor(0x1B, 0x1E, 0x23));
    setSceneRect(-5000, -5000, 10000, 10000);
    connect(this, &QGraphicsScene::selectionChanged,
            this, &NodeEditorScene::onSceneSelectionChanged);
}

NodeEditorScene::~NodeEditorScene()
{
    // The base QGraphicsScene destructor deletes the remaining items, which can
    // emit selectionChanged. By then this object is no longer a NodeEditorScene,
    // so our selection slot must not run — disconnect it first.
    disconnect(this, &QGraphicsScene::selectionChanged, nullptr, nullptr);
}

void NodeEditorScene::setGraph(Graph* graph)
{
    if (m_graph == graph)
        return;

    if (m_graph)
        m_graph->disconnect(this);

    m_graph = graph;
    rebuild();

    if (m_graph) {
        connect(m_graph, &Graph::nodeAdded,         this, &NodeEditorScene::onNodeAdded);
        connect(m_graph, &Graph::nodeRemoved,       this, &NodeEditorScene::onNodeRemoved);
        connect(m_graph, &Graph::connectionAdded,   this, &NodeEditorScene::onConnectionAdded);
        connect(m_graph, &Graph::connectionRemoved, this, &NodeEditorScene::onConnectionRemoved);
        connect(m_graph, &Graph::cleared,           this, &NodeEditorScene::onCleared);
        connect(m_graph, &Graph::nodePortsChanged,      this, &NodeEditorScene::onNodePortsChanged);
        connect(m_graph, &Graph::nodePropertiesChanged, this, &NodeEditorScene::onNodePropertiesChanged);
    }
}

NodeItem* NodeEditorScene::nodeItem(int nodeId) const
{
    return m_nodeItems.value(nodeId, nullptr);
}

void NodeEditorScene::rebuild()
{
    clear();                 // deletes all QGraphicsItems
    m_nodeItems.clear();
    m_connItems.clear();
    m_pendingConnection = nullptr;
    m_dragSource = nullptr;
    m_hoverTarget = nullptr;
    m_dropTarget = nullptr;

    if (!m_graph)
        return;
    for (const auto& n : m_graph->nodes())
        onNodeAdded(n.get());
    for (const auto& c : m_graph->connections())
        onConnectionAdded(c.get());
}

// --- Model -> view ----------------------------------------------------------

void NodeEditorScene::onNodeAdded(Node* node)
{
    auto* item = new NodeItem(node);
    addItem(item);
    m_nodeItems.insert(node->id, item);
    connect(item, &NodeItem::moved, this,
            [this, id = node->id] { updateConnectionsForNode(id); });
    connect(item, &NodeItem::fileChosen, this,
            [this, id = node->id](const QString& path) {
                if (m_graph) m_graph->setNodeLocation(id, path);
            });
}

void NodeEditorScene::onNodeRemoved(int nodeId)
{
    if (NodeItem* item = m_nodeItems.take(nodeId)) {
        removeItem(item);
        delete item;
    }
}

void NodeEditorScene::onConnectionAdded(Connection* connection)
{
    NodeItem* fromNode = nodeItem(connection->from.nodeId);
    NodeItem* toNode   = nodeItem(connection->to.nodeId);
    if (!fromNode || !toNode)
        return;

    PortItem* fromPort = fromNode->portItem(connection->from.direction, connection->from.index);
    PortItem* toPort   = toNode->portItem(connection->to.direction, connection->to.index);
    if (!fromPort || !toPort)
        return;

    auto* item = new ConnectionItem(connection->id, fromPort, toPort);
    addItem(item);
    m_connItems.insert(connection->id, item);
}

void NodeEditorScene::onConnectionRemoved(int connectionId)
{
    if (ConnectionItem* item = m_connItems.take(connectionId)) {
        removeItem(item);
        delete item;
    }
}

void NodeEditorScene::onCleared()
{
    rebuild();
}

void NodeEditorScene::onNodePortsChanged(Node* node)
{
    // Ports (and thus geometry and connections) changed: rebuild the items from
    // the model, then let the properties panel refresh.
    rebuild();
    emit nodeModified(node);
}

void NodeEditorScene::onNodePropertiesChanged(Node* node)
{
    if (NodeItem* item = nodeItem(node->id))
        item->update();
    emit nodeModified(node);
}

void NodeEditorScene::updateConnectionsForNode(int nodeId)
{
    if (!m_graph)
        return;
    for (const auto& c : m_graph->connections()) {
        if (c->from.nodeId == nodeId || c->to.nodeId == nodeId) {
            if (ConnectionItem* item = m_connItems.value(c->id, nullptr))
                item->updatePath();
        }
    }
}

// --- Selection --------------------------------------------------------------

void NodeEditorScene::onSceneSelectionChanged()
{
    Node* selected = nullptr;
    const QList<QGraphicsItem*> items = selectedItems();
    for (QGraphicsItem* it : items) {
        if (auto* n = qgraphicsitem_cast<NodeItem*>(it)) {
            selected = n->node();
            break;
        }
    }
    emit selectedNodeChanged(selected);
}

void NodeEditorScene::deleteSelection()
{
    if (!m_graph)
        return;

    const QList<QGraphicsItem*> items = selectedItems();
    // Collect ids first; deleting mutates the scene/selection.
    QList<int> nodeIds;
    QList<int> connIds;
    for (QGraphicsItem* it : items) {
        if (auto* n = qgraphicsitem_cast<NodeItem*>(it))
            nodeIds.append(n->nodeId());
        else if (auto* c = qgraphicsitem_cast<ConnectionItem*>(it))
            connIds.append(c->connectionId());
    }
    for (int id : connIds)
        m_graph->removeConnection(id);
    for (int id : nodeIds)
        m_graph->removeNode(id);
}

// --- Edge dragging ----------------------------------------------------------

PortItem* NodeEditorScene::portItemAt(QPointF scenePos) const
{
    const QList<QGraphicsItem*> hits = items(scenePos);
    for (QGraphicsItem* it : hits) {
        if (auto* p = qgraphicsitem_cast<PortItem*>(it))
            return p;
    }
    return nullptr;
}

void NodeEditorScene::promptValueTarget(Connection* conn)
{
    const Node* toNode = m_graph->node(conn->to.nodeId);
    const Port* toPort = toNode ? toNode->port(PortDirection::Input, conn->to.index)
                                : nullptr;
    if (!toPort || toPort->mediaType != MediaType::Value)
        return;   // not a value edge into an aux input — nothing to bind

    // Offer both the element's introspected properties and any synthetic node
    // properties (e.g. the File Output's name/directory/extension).
    QStringList names;
    for (const ElementProperty& p : ElementProperties::forElement(toNode->typeId))
        names << p.name;
    for (auto it = toNode->properties.constBegin();
         it != toNode->properties.constEnd(); ++it) {
        if (!names.contains(it.key()))
            names << it.key();
    }
    names.sort();

    if (names.isEmpty()) {
        m_graph->removeConnection(conn->id);   // nothing drivable here
        return;
    }

    QMenu menu;
    menu.addSection(tr("Drive which property?"));
    for (const QString& name : names) {
        QAction* action = menu.addAction(name);
        action->setData(name);
    }

    if (QAction* chosen = menu.exec(QCursor::pos()))
        conn->targetProperty = chosen->data().toString();
    else
        m_graph->removeConnection(conn->id);   // cancelled → drop the edge
}

void NodeEditorScene::clearDragHighlight()
{
    if (m_hoverTarget) {
        m_hoverTarget->setHighlighted(false);
        m_hoverTarget = nullptr;
    }
}

void NodeEditorScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (PortItem* port = portItemAt(event->scenePos())) {
            m_dragSource = port;
            m_pendingConnection = new ConnectionItem(port);
            addItem(m_pendingConnection);
            m_pendingConnection->setFreeEnd(event->scenePos());
            event->accept();
            return;
        }
    }
    QGraphicsScene::mousePressEvent(event);
}

void NodeEditorScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_pendingConnection) {
        m_pendingConnection->setFreeEnd(event->scenePos());

        PortItem* target = portItemAt(event->scenePos());
        if (target != m_hoverTarget) {
            clearDragHighlight();
            if (target && target != m_dragSource && m_graph &&
                m_graph->canConnect(m_dragSource->portRef(), target->portRef())) {
                m_hoverTarget = target;
                m_hoverTarget->setHighlighted(true);
            }
        }
        event->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}

void NodeEditorScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_pendingConnection) {
        removeItem(m_pendingConnection);
        delete m_pendingConnection;
        m_pendingConnection = nullptr;

        clearDragHighlight();

        if (PortItem* target = portItemAt(event->scenePos());
            target && m_dragSource && target != m_dragSource && m_graph) {
            if (Connection* conn = m_graph->addConnection(m_dragSource->portRef(),
                                                          target->portRef()))
                promptValueTarget(conn);
        }
        m_dragSource = nullptr;
        event->accept();
        return;
    }
    QGraphicsScene::mouseReleaseEvent(event);
}

// --- Context menu (delete) --------------------------------------------------

void NodeEditorScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    if (!m_graph)
        return;

    QGraphicsItem* hit = itemAt(event->scenePos(), QTransform());
    // Walk up to the owning node if a child (port) was hit.
    NodeItem*       node = nullptr;
    ConnectionItem* conn = nullptr;
    for (QGraphicsItem* it = hit; it; it = it->parentItem()) {
        if ((node = qgraphicsitem_cast<NodeItem*>(it))) break;
        if ((conn = qgraphicsitem_cast<ConnectionItem*>(it))) break;
    }

    QMenu menu;
    if (node) {
        QAction* del = menu.addAction(tr("Delete node"));
        connect(del, &QAction::triggered, this,
                [this, id = node->nodeId()] { m_graph->removeNode(id); });
    } else if (conn) {
        QAction* del = menu.addAction(tr("Delete connection"));
        connect(del, &QAction::triggered, this,
                [this, id = conn->connectionId()] { m_graph->removeConnection(id); });
    } else {
        QAction* del = menu.addAction(tr("Delete selection"));
        del->setEnabled(!selectedItems().isEmpty());
        connect(del, &QAction::triggered, this, [this] { deleteSelection(); });
    }
    menu.exec(event->screenPos());
}

// --- File drag and drop -----------------------------------------------------

namespace {
bool hasLocalFileUrls(const QMimeData* mime)
{
    if (!mime || !mime->hasUrls())
        return false;
    for (const QUrl& url : mime->urls())
        if (url.isLocalFile())
            return true;
    return false;
}

bool hasNodeType(const QMimeData* mime)
{
    return mime && mime->hasFormat(QLatin1String(kNodeMimeType));
}

bool acceptsDrag(const QMimeData* mime)
{
    return hasLocalFileUrls(mime) || hasNodeType(mime);
}
} // namespace

NodeItem* NodeEditorScene::fileNodeItemAt(QPointF scenePos) const
{
    const QList<QGraphicsItem*> hits = items(scenePos);
    for (QGraphicsItem* it : hits) {
        if (auto* node = qgraphicsitem_cast<NodeItem*>(it); node && node->hasFileField())
            return node;
    }
    return nullptr;
}

void NodeEditorScene::setDropTarget(NodeItem* item)
{
    if (m_dropTarget == item)
        return;
    if (m_dropTarget)
        m_dropTarget->setDropHighlight(false);
    m_dropTarget = item;
    if (m_dropTarget)
        m_dropTarget->setDropHighlight(true);
}

void NodeEditorScene::dragEnterEvent(QGraphicsSceneDragDropEvent* event)
{
    if (acceptsDrag(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        QGraphicsScene::dragEnterEvent(event);
    }
}

void NodeEditorScene::dragMoveEvent(QGraphicsSceneDragDropEvent* event)
{
    if (acceptsDrag(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        // Highlight a file node only when dragging files onto it.
        setDropTarget(hasLocalFileUrls(event->mimeData())
                          ? fileNodeItemAt(event->scenePos()) : nullptr);
    } else {
        QGraphicsScene::dragMoveEvent(event);
    }
}

void NodeEditorScene::dragLeaveEvent(QGraphicsSceneDragDropEvent* event)
{
    setDropTarget(nullptr);
    QGraphicsScene::dragLeaveEvent(event);
}

void NodeEditorScene::dropEvent(QGraphicsSceneDragDropEvent* event)
{
    setDropTarget(nullptr);

    if (!m_graph) {
        QGraphicsScene::dropEvent(event);
        return;
    }

    // A node type dragged from the palette: create it where it was dropped.
    if (hasNodeType(event->mimeData())) {
        const QString typeId = QString::fromUtf8(
            event->mimeData()->data(QLatin1String(kNodeMimeType)));
        m_graph->addNode(typeId, event->scenePos());
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }

    if (!hasLocalFileUrls(event->mimeData())) {
        QGraphicsScene::dropEvent(event);
        return;
    }

    QStringList files;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            files << url.toLocalFile();
    }
    if (files.isEmpty())
        return;

    if (NodeItem* target = fileNodeItemAt(event->scenePos())) {
        // Dropped onto an existing file node: assign the first file.
        m_graph->setNodeLocation(target->nodeId(), files.first());
    } else {
        // Dropped on empty canvas: spawn a File Source per file.
        qreal offset = 0.0;
        for (const QString& file : files) {
            Node* node = m_graph->addNode(QStringLiteral("source.file"),
                                          event->scenePos() + QPointF(offset, offset));
            if (node)
                m_graph->setNodeLocation(node->id, file);
            offset += 28.0;
        }
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

} // namespace ab
