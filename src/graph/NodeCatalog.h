#pragma once

#include "graph/Types.h"

#include <QColor>
#include <QString>
#include <QVariantMap>

#include <vector>

namespace ab {

/// Describes a single port on a node template.
struct PortSpec {
    QString   name;
    MediaType mediaType = MediaType::Any;
};

/// A template from which concrete Nodes are instantiated. Each spec maps to one
/// or more GStreamer elements (resolved later by the execution backend).
struct NodeTypeSpec {
    QString               typeId;       ///< e.g. "x264enc"
    QString               displayName;  ///< short label (the element name)
    QString               longName;     ///< GStreamer long name, e.g. "x264 H.264 Encoder"
    QString               description;  ///< what the element does (GStreamer metadata)
    QString               category;     ///< e.g. "Encoders"
    QColor                color;
    std::vector<PortSpec> inputs;
    std::vector<PortSpec> outputs;
    QVariantMap           defaultProperties;
};

/// Value outputs a Stream Inspector node exposes for a given input stream type.
std::vector<PortSpec> inspectorValueOutputs(MediaType type);

/// Registry of all available node types. Singleton, populated at construction.
class NodeCatalog {
public:
    static const NodeCatalog& instance();

    const std::vector<NodeTypeSpec>& specs() const { return m_specs; }
    const NodeTypeSpec*              find(const QString& typeId) const;

    /// Unique, ordered list of categories (in registration order).
    std::vector<QString> categories() const;

private:
    NodeCatalog();
    void registerSpec(NodeTypeSpec spec);

    /// Builds Filters/Encoders/Muxers/Sinks from the GStreamer registry
    /// (includes libav-backed elements). Only compiled with GStreamer.
    void populateFromGStreamer();
    /// Minimal hard-coded set used when built without GStreamer.
    void populateStatic();

    std::vector<NodeTypeSpec> m_specs;
};

} // namespace ab
