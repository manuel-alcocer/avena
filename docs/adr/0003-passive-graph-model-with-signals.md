# 3. Keep the logical model passive, owned by Graph, observed via signals

- Status: Accepted
- Date: 2026-06-21

## Context

Given the three-layer split ([ADR-0002](0002-three-layer-pipeline-architecture.md)),
the canvas and the model must stay in sync without the model depending on the
canvas. We also need clear ownership of nodes and connections so there are no
leaks or dangling references as the user adds and removes them.

## Decision

`Graph` is the single owner of all `Node`s and `Connection`s, held by
`std::unique_ptr` in insertion-ordered vectors. `Node`, `Port`, and `Connection`
are passive data objects: a `Node` does not know about its `NodeItem` or its
GStreamer element.

`Graph` is a `QObject` and is the only writable entry point to the model. It
emits change signals — `nodeAdded`, `nodeRemoved`, `connectionAdded`,
`connectionRemoved`, `cleared`, `nodePortsChanged`, `nodePropertiesChanged`. The
canvas holds **raw observing pointers** into the model and reacts to these
signals; ownership never flows back from canvas to model.

## Consequences

- Lifetimes are unambiguous: when `Graph` drops a node, it emits a removal signal
  first so observers can detach before the object is destroyed.
- The canvas is a pure projection of the model; rebuilding the view from a freshly
  loaded `Graph` is just replaying "added" signals.
- Raw observing pointers are safe **only** because every removal is signalled;
  any code holding a `Node*`/`Connection*` must listen for the matching removal.
- Mutations go through `Graph`'s API, not by poking nodes directly, so invariants
  stay enforced in one place — see
  [ADR-0005](0005-centralized-typed-connection-rules.md).
