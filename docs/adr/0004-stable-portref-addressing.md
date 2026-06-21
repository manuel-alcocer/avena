# 4. Address ports by stable index-based PortRef, not pointers

- Status: Accepted
- Date: 2026-06-21

## Context

A connection joins two ports. We need a way to name "this port on that node" that
(a) survives saving and loading, and (b) does not break when a node's port list
is rebuilt — which happens routinely: setting a File Source's location re-probes
the media and regenerates its output ports to match the streams found.

Pointers to `Port` objects would dangle the moment a node's port vector is
reallocated, and they cannot be serialized.

## Decision

Ports are addressed by a value type, `PortRef { int nodeId, PortDirection
direction, int index }`, where `index` is the position within the node's `inputs`
or `outputs` list. `Connection` stores its endpoints as two `PortRef`s (`from` =
Output, `to` = Input).

Node ids are unique and stable for the node's lifetime (assigned by `Graph`).
Port indices are stable only as long as the port list is unchanged; rebuilding a
node's ports may invalidate existing `PortRef`s by design.

## Consequences

- Connections serialize trivially (ids + indices) and never carry raw pointers.
- When a node's ports are rebuilt, `Graph` drops the now-invalid connections and
  emits `nodePortsChanged` / `connectionRemoved`. This is intentional behaviour,
  not an error path: re-probing a source can legitimately remove a stream that a
  connection depended on.
- Code consuming a `PortRef` must resolve it through `Graph` (`node()`, `port()`)
  and tolerate a miss, rather than caching a resolved pointer across mutations.
