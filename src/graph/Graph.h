#pragma once

#include "graph/Connection.h"
#include "graph/Node.h"
#include "graph/Port.h"

#include <QObject>

#include <memory>
#include <vector>

namespace ab {

/// The logical pipeline graph: owns nodes and connections, enforces connection
/// rules, and emits change signals the canvas listens to.
///
/// Ownership is exclusive (unique_ptr); the canvas holds raw observing pointers.
class Graph : public QObject {
    Q_OBJECT

public:
    explicit Graph(QObject* parent = nullptr);
    ~Graph() override;

    // --- Nodes ---------------------------------------------------------------

    /// Creates a node from a catalog type id. Returns nullptr if the type is
    /// unknown.
    Node* addNode(const QString& typeId, QPointF position);

    /// Re-inserts a fully described node (used by the deserializer). Takes
    /// ownership. The node's `id` must be unique and > 0.
    Node* insertNode(std::unique_ptr<Node> node);

    /// Sets a node's `location` property. For `source.file` nodes this probes
    /// the media and rebuilds the output ports to match its streams
    /// (video/audio/subtitle), dropping any now-invalid outgoing connections.
    void setNodeLocation(int nodeId, const QString& path);

    /// Rebuilds a Stream Inspector's value outputs (after its enable flags or
    /// input connection change).
    void refreshInspectorPorts(int nodeId) { updateInspectorPorts(nodeId); }

    void  removeNode(int nodeId);
    Node* node(int nodeId) const;

    const std::vector<std::unique_ptr<Node>>& nodes() const { return m_nodes; }

    // --- Connections ---------------------------------------------------------

    /// Validates a prospective connection. `from` must be an Output, `to` an
    /// Input, on different nodes, type-compatible, and the input must be free.
    bool canConnect(const PortRef& from, const PortRef& to,
                    QString* reason = nullptr) const;

    /// Adds a connection if valid. Orientation of the two refs is normalized
    /// automatically (output→input). Returns nullptr if invalid.
    Connection* addConnection(const PortRef& a, const PortRef& b);

    Connection* insertConnection(std::unique_ptr<Connection> connection);

    void removeConnection(int connectionId);

    const std::vector<std::unique_ptr<Connection>>& connections() const
    {
        return m_connections;
    }

    /// True if the given input port already has an incoming connection.
    bool isInputOccupied(const PortRef& input) const;

    // --- Bulk ----------------------------------------------------------------

    void clear();

    int  peekNextNodeId() const { return m_nextNodeId; }
    void ensureNodeIdAbove(int id);
    void ensureConnectionIdAbove(int id);

signals:
    void nodeAdded(ab::Node* node);
    void nodeRemoved(int nodeId);
    void connectionAdded(ab::Connection* connection);
    void connectionRemoved(int connectionId);
    void cleared();

    /// A node's port layout changed (e.g. a source file's streams were probed).
    void nodePortsChanged(ab::Node* node);
    /// A node's properties changed without affecting ports.
    void nodePropertiesChanged(ab::Node* node);

private:
    void removeConnectionsTouching(int nodeId);
    void removeOutgoingConnections(int nodeId);
    /// Rebuilds a Stream Inspector's value outputs from its connected stream.
    void updateInspectorPorts(int nodeId);

    std::vector<std::unique_ptr<Node>>       m_nodes;
    std::vector<std::unique_ptr<Connection>> m_connections;
    int m_nextNodeId = 1;
    int m_nextConnId = 1;
};

} // namespace ab
