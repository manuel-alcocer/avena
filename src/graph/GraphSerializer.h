#pragma once

#include <QJsonObject>
#include <QString>

namespace ab {

class Graph;

/// Reads/writes a Graph to/from a JSON document.
///
/// The on-disk format is a small, stable schema:
/// { "version": 1, "nodes": [...], "connections": [...] }
namespace GraphSerializer {

QJsonObject toJson(const Graph& graph);

/// Replaces the contents of `graph` with the deserialized document.
/// Returns false and sets `error` on a malformed document.
bool fromJson(const QJsonObject& root, Graph& graph, QString* error = nullptr);

bool saveToFile(const Graph& graph, const QString& path, QString* error = nullptr);
bool loadFromFile(Graph& graph, const QString& path, QString* error = nullptr);

} // namespace GraphSerializer
} // namespace ab
