#pragma once

#include "graph/Port.h"

namespace ab {

/// A directed edge between an output port (`from`) and an input port (`to`).
struct Connection {
    int     id = -1;
    PortRef from;   ///< Always an Output port.
    PortRef to;     ///< Always an Input port.

    /// For value (parameter) edges into a node's aux input: the GObject property
    /// of the destination node that this value drives (chosen at connect time).
    QString targetProperty;
};

} // namespace ab
