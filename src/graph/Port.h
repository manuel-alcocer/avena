#pragma once

#include "graph/Types.h"

#include <QString>

namespace ab {

/// A single connection point on a node.
struct Port {
    QString   name;
    MediaType mediaType = MediaType::Any;
};

/// A stable reference to a specific port on a specific node.
/// Connections are expressed in terms of PortRef so they survive serialization
/// and never dangle on raw pointers.
struct PortRef {
    int           nodeId = -1;
    PortDirection direction = PortDirection::Input;
    int           index = -1;

    [[nodiscard]] bool isValid() const { return nodeId >= 0 && index >= 0; }

    friend bool operator==(const PortRef&, const PortRef&) = default;
};

} // namespace ab
