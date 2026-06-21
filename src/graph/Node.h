#pragma once

#include "graph/Port.h"

#include <QColor>
#include <QPointF>
#include <QString>
#include <QVariantMap>

#include <vector>

namespace ab {

/// A node in the pipeline graph. This is a passive data object; the visual
/// representation (NodeItem) and the GStreamer mapping live elsewhere.
///
/// Ports are addressed by their index within `inputs` / `outputs`. The owning
/// Graph assigns the (unique, stable) `id`.
class Node {
public:
    int     id = -1;
    QString typeId;     ///< Catalog type, e.g. "encoder.h264".
    QString title;      ///< User-visible label.
    QString category;   ///< Catalog category, e.g. "Encoders".
    QColor  color;      ///< Header colour, derived from the catalog.
    QPointF position;   ///< Scene position (top-left of the node).
    double  width = 0;  ///< User width override; 0 means auto (default width).

    std::vector<Port> inputs;
    std::vector<Port> outputs;

    /// Free-form element parameters (bitrate, width, location, ...).
    QVariantMap properties;

    [[nodiscard]] const std::vector<Port>& ports(PortDirection dir) const
    {
        return dir == PortDirection::Input ? inputs : outputs;
    }

    [[nodiscard]] const Port* port(PortDirection dir, int index) const
    {
        const auto& list = ports(dir);
        if (index < 0 || index >= static_cast<int>(list.size()))
            return nullptr;
        return &list[static_cast<std::size_t>(index)];
    }
};

} // namespace ab
