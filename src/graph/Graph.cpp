#include "graph/Graph.h"

#include "graph/NodeCatalog.h"
#include "media/MediaInfo.h"

#include <algorithm>

namespace ab {

Graph::Graph(QObject* parent) : QObject(parent) {}
Graph::~Graph() = default;

// --- Nodes ------------------------------------------------------------------

Node* Graph::addNode(const QString& typeId, QPointF position)
{
    const NodeTypeSpec* spec = NodeCatalog::instance().find(typeId);
    if (!spec)
        return nullptr;

    auto node = std::make_unique<Node>();
    node->id       = m_nextNodeId++;
    node->typeId   = spec->typeId;
    node->title    = spec->displayName;
    node->category = spec->category;
    node->color    = spec->color;
    node->position = position;
    node->properties = spec->defaultProperties;

    for (const PortSpec& p : spec->inputs)
        node->inputs.push_back(Port{p.name, p.mediaType});
    for (const PortSpec& p : spec->outputs)
        node->outputs.push_back(Port{p.name, p.mediaType});

    Node* raw = node.get();
    m_nodes.push_back(std::move(node));
    emit nodeAdded(raw);
    return raw;
}

Node* Graph::insertNode(std::unique_ptr<Node> node)
{
    if (!node || node->id <= 0)
        return nullptr;
    ensureNodeIdAbove(node->id);
    Node* raw = node.get();
    m_nodes.push_back(std::move(node));
    emit nodeAdded(raw);
    return raw;
}

namespace {

/// Builds the output ports for a File Source from the probed media streams.
/// Falls back to a generic video+audio pair when the file can't be analysed.
std::vector<Port> sourceOutputsFor(const QString& path)
{
    const MediaInfo info = MediaProbe::probe(path);

    int videoCount = 0, audioCount = 0, subCount = 0;
    for (const MediaStreamInfo& s : info.streams) {
        switch (s.kind) {
        case MediaStreamInfo::Kind::Video:    ++videoCount; break;
        case MediaStreamInfo::Kind::Audio:    ++audioCount; break;
        case MediaStreamInfo::Kind::Subtitle: ++subCount;   break;
        case MediaStreamInfo::Kind::Other:    break;
        }
    }

    auto name = [](const QString& base, int index, int total) {
        return total > 1 ? QStringLiteral("%1 %2").arg(base).arg(index) : base;
    };

    std::vector<Port> outputs;
    int vi = 0, ai = 0, si = 0;
    for (const MediaStreamInfo& s : info.streams) {
        switch (s.kind) {
        case MediaStreamInfo::Kind::Video:
            outputs.push_back({name(QStringLiteral("video"), vi++, videoCount),
                               MediaType::Video});
            break;
        case MediaStreamInfo::Kind::Audio:
            outputs.push_back({name(QStringLiteral("audio"), ai++, audioCount),
                               MediaType::Audio});
            break;
        case MediaStreamInfo::Kind::Subtitle:
            outputs.push_back({name(QStringLiteral("subtitle"), si++, subCount),
                               MediaType::Subtitle});
            break;
        case MediaStreamInfo::Kind::Other:
            break;
        }
    }

    if (outputs.empty()) {
        outputs.push_back({QStringLiteral("video"), MediaType::Video});
        outputs.push_back({QStringLiteral("audio"), MediaType::Audio});
    }
    return outputs;
}

} // namespace

void Graph::setNodeLocation(int nodeId, const QString& path)
{
    Node* n = node(nodeId);
    if (!n)
        return;

    n->properties[QStringLiteral("location")] = path;

    if (n->typeId == QLatin1String("source.file")) {
        const std::vector<Port> oldOutputs = n->outputs;
        std::vector<Port> newOutputs = sourceOutputsFor(path);

        // Keep outgoing wires whose output index still maps to a same-type
        // stream; only drop the ones that no longer line up.
        for (auto it = m_connections.begin(); it != m_connections.end();) {
            const Connection* c = it->get();
            if (c->from.nodeId == nodeId) {
                const int idx = c->from.index;
                const bool stillValid =
                    idx >= 0 &&
                    idx < static_cast<int>(newOutputs.size()) &&
                    idx < static_cast<int>(oldOutputs.size()) &&
                    newOutputs[static_cast<std::size_t>(idx)].mediaType ==
                        oldOutputs[static_cast<std::size_t>(idx)].mediaType;
                if (!stillValid) {
                    const int id = c->id;
                    it = m_connections.erase(it);
                    emit connectionRemoved(id);
                    continue;
                }
            }
            ++it;
        }

        n->outputs = std::move(newOutputs);
        emit nodePortsChanged(n);
    } else {
        emit nodePropertiesChanged(n);
    }
}

void Graph::removeNode(int nodeId)
{
    auto it = std::ranges::find_if(m_nodes,
        [&](const auto& n) { return n->id == nodeId; });
    if (it == m_nodes.end())
        return;

    removeConnectionsTouching(nodeId);
    m_nodes.erase(it);
    emit nodeRemoved(nodeId);
}

Node* Graph::node(int nodeId) const
{
    auto it = std::ranges::find_if(m_nodes,
        [&](const auto& n) { return n->id == nodeId; });
    return it == m_nodes.end() ? nullptr : it->get();
}

// --- Connections ------------------------------------------------------------

bool Graph::isInputOccupied(const PortRef& input) const
{
    return std::ranges::any_of(m_connections,
        [&](const auto& c) { return c->to == input; });
}

bool Graph::canConnect(const PortRef& from, const PortRef& to,
                       QString* reason) const
{
    auto fail = [&](const QString& msg) {
        if (reason) *reason = msg;
        return false;
    };

    if (!from.isValid() || !to.isValid())
        return fail(QStringLiteral("Invalid port."));
    if (from.direction != PortDirection::Output)
        return fail(QStringLiteral("Source must be an output port."));
    if (to.direction != PortDirection::Input)
        return fail(QStringLiteral("Target must be an input port."));
    if (from.nodeId == to.nodeId)
        return fail(QStringLiteral("Cannot connect a node to itself."));

    const Node* fromNode = node(from.nodeId);
    const Node* toNode   = node(to.nodeId);
    if (!fromNode || !toNode)
        return fail(QStringLiteral("Unknown node."));

    const Port* fromPort = fromNode->port(PortDirection::Output, from.index);
    const Port* toPort   = toNode->port(PortDirection::Input, to.index);
    if (!fromPort || !toPort)
        return fail(QStringLiteral("Unknown port."));

    if (!mediaTypesCompatible(fromPort->mediaType, toPort->mediaType))
        return fail(QStringLiteral("Incompatible media types."));
    if (isInputOccupied(to))
        return fail(QStringLiteral("Input is already connected."));

    return true;
}

Connection* Graph::addConnection(const PortRef& a, const PortRef& b)
{
    // Normalize so `from` is the output and `to` is the input.
    PortRef from = a;
    PortRef to   = b;
    if (from.direction == PortDirection::Input)
        std::swap(from, to);

    if (!canConnect(from, to))
        return nullptr;

    auto conn = std::make_unique<Connection>();
    conn->id   = m_nextConnId++;
    conn->from = from;
    conn->to   = to;

    Connection* raw = conn.get();
    m_connections.push_back(std::move(conn));
    emit connectionAdded(raw);

    // Feeding an Inspector's media input refreshes its value outputs.
    if (const Node* toNode = node(to.nodeId);
        toNode && toNode->typeId == QLatin1String("tool.inspector") && to.index == 0)
        updateInspectorPorts(to.nodeId);

    return raw;
}

void Graph::updateInspectorPorts(int nodeId)
{
    Node* n = node(nodeId);
    if (!n || n->typeId != QLatin1String("tool.inspector"))
        return;

    // Stream type feeding the inspector's media input (index 0).
    MediaType type = MediaType::Any;
    for (const auto& c : m_connections) {
        if (c->to.nodeId == nodeId && c->to.index == 0) {
            if (const Node* up = node(c->from.nodeId))
                if (const Port* p = up->port(PortDirection::Output, c->from.index))
                    type = p->mediaType;
            break;
        }
    }

    // Drop existing value-output wires (indices >= 1); keep the media passthrough.
    for (auto it = m_connections.begin(); it != m_connections.end();) {
        if ((*it)->from.nodeId == nodeId && (*it)->from.index >= 1) {
            const int id = (*it)->id;
            it = m_connections.erase(it);
            emit connectionRemoved(id);
        } else {
            ++it;
        }
    }

    n->outputs.clear();
    n->outputs.push_back({QStringLiteral("out"), MediaType::Any});
    for (const PortSpec& ps : inspectorValueOutputs(type)) {
        // A value output only appears when enabled in the node's properties,
        // keeping the node compact when only a few are used.
        if (n->properties.value(QStringLiteral("enable:") + ps.name).toBool())
            n->outputs.push_back({ps.name, ps.mediaType});
    }

    emit nodePortsChanged(n);
}

Connection* Graph::insertConnection(std::unique_ptr<Connection> connection)
{
    if (!connection || connection->id <= 0)
        return nullptr;
    ensureConnectionIdAbove(connection->id);
    Connection* raw = connection.get();
    m_connections.push_back(std::move(connection));
    emit connectionAdded(raw);
    return raw;
}

void Graph::removeConnection(int connectionId)
{
    auto it = std::ranges::find_if(m_connections,
        [&](const auto& c) { return c->id == connectionId; });
    if (it == m_connections.end())
        return;

    // Remember if this fed an Inspector's media input, to refresh it afterwards.
    int inspectorToRefresh = -1;
    if (const Node* toNode = node((*it)->to.nodeId);
        toNode && toNode->typeId == QLatin1String("tool.inspector") &&
        (*it)->to.index == 0)
        inspectorToRefresh = (*it)->to.nodeId;

    m_connections.erase(it);
    emit connectionRemoved(connectionId);

    if (inspectorToRefresh >= 0)
        updateInspectorPorts(inspectorToRefresh);
}

void Graph::removeConnectionsTouching(int nodeId)
{
    for (auto it = m_connections.begin(); it != m_connections.end();) {
        if ((*it)->from.nodeId == nodeId || (*it)->to.nodeId == nodeId) {
            const int id = (*it)->id;
            it = m_connections.erase(it);
            emit connectionRemoved(id);
        } else {
            ++it;
        }
    }
}

void Graph::removeOutgoingConnections(int nodeId)
{
    for (auto it = m_connections.begin(); it != m_connections.end();) {
        if ((*it)->from.nodeId == nodeId) {
            const int id = (*it)->id;
            it = m_connections.erase(it);
            emit connectionRemoved(id);
        } else {
            ++it;
        }
    }
}

// --- Bulk -------------------------------------------------------------------

void Graph::clear()
{
    m_connections.clear();
    m_nodes.clear();
    m_nextNodeId = 1;
    m_nextConnId = 1;
    emit cleared();
}

void Graph::ensureNodeIdAbove(int id)
{
    if (id >= m_nextNodeId)
        m_nextNodeId = id + 1;
}

void Graph::ensureConnectionIdAbove(int id)
{
    if (id >= m_nextConnId)
        m_nextConnId = id + 1;
}

} // namespace ab
