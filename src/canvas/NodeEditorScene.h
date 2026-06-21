#pragma once

#include <QGraphicsScene>
#include <QHash>

namespace ab {

class Graph;
class Node;
class Connection;
class NodeItem;
class ConnectionItem;
class PortItem;

/// The QGraphicsScene that mirrors a Graph: it creates/destroys visual items in
/// response to the model's signals, and translates mouse interaction (dragging
/// edges between ports, selecting and deleting) back into model mutations.
class NodeEditorScene : public QGraphicsScene {
    Q_OBJECT

public:
    explicit NodeEditorScene(QObject* parent = nullptr);
    ~NodeEditorScene() override;

    void   setGraph(Graph* graph);
    Graph* graph() const { return m_graph; }

    NodeItem* nodeItem(int nodeId) const;

    /// Deletes everything currently selected (nodes and connections).
    void deleteSelection();

signals:
    /// Emitted when the set of selected nodes changes (for the properties panel).
    void selectedNodeChanged(ab::Node* node);

    /// Emitted when a node's properties change in-place (e.g. file location).
    void nodeModified(ab::Node* node);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
    void dragEnterEvent(QGraphicsSceneDragDropEvent* event) override;
    void dragMoveEvent(QGraphicsSceneDragDropEvent* event) override;
    void dragLeaveEvent(QGraphicsSceneDragDropEvent* event) override;
    void dropEvent(QGraphicsSceneDragDropEvent* event) override;

private slots:
    void onNodeAdded(ab::Node* node);
    void onNodeRemoved(int nodeId);
    void onConnectionAdded(ab::Connection* connection);
    void onConnectionRemoved(int connectionId);
    void onCleared();
    void onSceneSelectionChanged();
    void onNodePortsChanged(ab::Node* node);
    void onNodePropertiesChanged(ab::Node* node);

private:
    void rebuild();
    void updateConnectionsForNode(int nodeId);
    PortItem* portItemAt(QPointF scenePos) const;
    void clearDragHighlight();

    NodeItem* fileNodeItemAt(QPointF scenePos) const;
    void      setDropTarget(NodeItem* item);
    /// For a value edge into an aux input, asks which target property it drives.
    void      promptValueTarget(Connection* conn);

    Graph* m_graph = nullptr;

    QHash<int, NodeItem*>       m_nodeItems;
    QHash<int, ConnectionItem*> m_connItems;

    // Edge-dragging state.
    ConnectionItem* m_pendingConnection = nullptr;
    PortItem*       m_dragSource = nullptr;
    PortItem*       m_hoverTarget = nullptr;

    // File drag-and-drop state.
    NodeItem* m_dropTarget = nullptr;
};

} // namespace ab
