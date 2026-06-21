#include "graph/GraphSerializer.h"

#include "graph/Graph.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace ab {
namespace GraphSerializer {

namespace {

constexpr int kSchemaVersion = 1;

QJsonObject portRefToJson(const PortRef& ref)
{
    return QJsonObject{
        {QStringLiteral("node"),      ref.nodeId},
        {QStringLiteral("direction"), portDirectionId(ref.direction)},
        {QStringLiteral("index"),     ref.index},
    };
}

PortRef portRefFromJson(const QJsonObject& obj)
{
    PortRef ref;
    ref.nodeId    = obj.value(QStringLiteral("node")).toInt(-1);
    ref.direction = portDirectionFromId(obj.value(QStringLiteral("direction")).toString());
    ref.index     = obj.value(QStringLiteral("index")).toInt(-1);
    return ref;
}

QJsonArray portsToJson(const std::vector<Port>& ports)
{
    QJsonArray arr;
    for (const Port& p : ports) {
        arr.append(QJsonObject{
            {QStringLiteral("name"), p.name},
            {QStringLiteral("type"), mediaTypeId(p.mediaType)},
        });
    }
    return arr;
}

void portsFromJson(const QJsonArray& arr, std::vector<Port>& out)
{
    out.clear();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        out.push_back(Port{
            o.value(QStringLiteral("name")).toString(),
            mediaTypeFromId(o.value(QStringLiteral("type")).toString()),
        });
    }
}

} // namespace

QJsonObject toJson(const Graph& graph)
{
    QJsonArray nodes;
    for (const auto& n : graph.nodes()) {
        nodes.append(QJsonObject{
            {QStringLiteral("id"),       n->id},
            {QStringLiteral("type"),     n->typeId},
            {QStringLiteral("title"),    n->title},
            {QStringLiteral("category"), n->category},
            {QStringLiteral("color"),    n->color.name(QColor::HexRgb)},
            {QStringLiteral("x"),        n->position.x()},
            {QStringLiteral("y"),        n->position.y()},
            {QStringLiteral("width"),    n->width},
            {QStringLiteral("inputs"),   portsToJson(n->inputs)},
            {QStringLiteral("outputs"),  portsToJson(n->outputs)},
            {QStringLiteral("properties"), QJsonObject::fromVariantMap(n->properties)},
        });
    }

    QJsonArray connections;
    for (const auto& c : graph.connections()) {
        QJsonObject co{
            {QStringLiteral("id"),   c->id},
            {QStringLiteral("from"), portRefToJson(c->from)},
            {QStringLiteral("to"),   portRefToJson(c->to)},
        };
        if (!c->targetProperty.isEmpty())
            co.insert(QStringLiteral("targetProperty"), c->targetProperty);
        connections.append(co);
    }

    return QJsonObject{
        {QStringLiteral("version"),     kSchemaVersion},
        {QStringLiteral("nodes"),       nodes},
        {QStringLiteral("connections"), connections},
    };
}

bool fromJson(const QJsonObject& root, Graph& graph, QString* error)
{
    auto fail = [&](const QString& msg) {
        if (error) *error = msg;
        return false;
    };

    const int version = root.value(QStringLiteral("version")).toInt(0);
    if (version != kSchemaVersion)
        return fail(QStringLiteral("Unsupported document version: %1").arg(version));

    graph.clear();

    for (const QJsonValue& v : root.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject o = v.toObject();
        auto node = std::make_unique<Node>();
        node->id       = o.value(QStringLiteral("id")).toInt(-1);
        node->typeId   = o.value(QStringLiteral("type")).toString();
        node->title    = o.value(QStringLiteral("title")).toString();
        node->category = o.value(QStringLiteral("category")).toString();
        node->color    = QColor(o.value(QStringLiteral("color")).toString());
        node->position = QPointF(o.value(QStringLiteral("x")).toDouble(),
                                 o.value(QStringLiteral("y")).toDouble());
        node->width = o.value(QStringLiteral("width")).toDouble(0.0);
        portsFromJson(o.value(QStringLiteral("inputs")).toArray(),  node->inputs);
        portsFromJson(o.value(QStringLiteral("outputs")).toArray(), node->outputs);
        node->properties = o.value(QStringLiteral("properties")).toObject().toVariantMap();

        if (node->id <= 0)
            return fail(QStringLiteral("Node with invalid id."));
        if (!graph.insertNode(std::move(node)))
            return fail(QStringLiteral("Failed to insert node."));
    }

    for (const QJsonValue& v : root.value(QStringLiteral("connections")).toArray()) {
        const QJsonObject o = v.toObject();
        auto conn = std::make_unique<Connection>();
        conn->id   = o.value(QStringLiteral("id")).toInt(-1);
        conn->from = portRefFromJson(o.value(QStringLiteral("from")).toObject());
        conn->to   = portRefFromJson(o.value(QStringLiteral("to")).toObject());
        conn->targetProperty = o.value(QStringLiteral("targetProperty")).toString();
        if (conn->id <= 0)
            return fail(QStringLiteral("Connection with invalid id."));
        graph.insertConnection(std::move(conn));
    }

    return true;
}

bool saveToFile(const Graph& graph, const QString& path, QString* error)
{
    const QJsonDocument doc(toJson(graph));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool loadFromFile(Graph& graph, const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) *error = parseError.errorString();
        return false;
    }
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("Document root is not an object.");
        return false;
    }
    return fromJson(doc.object(), graph, error);
}

} // namespace GraphSerializer
} // namespace ab
