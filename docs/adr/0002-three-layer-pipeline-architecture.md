# 2. Separate the pipeline into three layers

- Status: Accepted
- Date: 2026-06-21

## Context

avena represents "the pipeline" in three different ways: the abstract graph the
user designs, the boxes-and-wires they manipulate on screen, and the concrete
GStreamer pipeline that actually runs. Early node editors often fuse these — the
on-screen widget *is* the model and also holds the GStreamer handle — which makes
the model impossible to test headlessly, couples UI code to the media engine, and
turns serialization into screen-scraping.

## Decision

We split the system into three layers with a one-directional dependency flow:

1. **Logical model** (`src/graph/`) — passive data describing *what* the pipeline
   is (`Graph`, `Node`, `Port`, `Connection`). No Qt widgets, no GStreamer. This
   is the source of truth and the unit of serialization.
2. **Canvas** (`src/canvas/`) — the `QGraphicsView` editor (`NodeItem`, `PortItem`,
   `ConnectionItem`, scene, view). Visual representations that observe the model
   and own no domain state.
3. **Execution backend** (`src/engine/`) — `PipelineRunner` translates the logical
   `Graph` into a GStreamer pipeline and runs it. The GStreamer mapping lives
   here and nowhere else.

The model knows nothing about the canvas or GStreamer; the canvas knows the model
but not GStreamer; the backend knows the model but not the canvas.

## Consequences

- The model can be built, serialized, and executed headlessly (`avena --run
  pipeline.abk`) with no UI — see [ADR-0008](0008-gstreamer-mapping-in-execution-backend.md).
- The media engine is replaceable and optional at build time — see
  [ADR-0007](0007-gstreamer-optional-build-dependency.md).
- A change to "the pipeline" must be made in the right layer; storing domain state
  on a `NodeItem` or a GStreamer element is a layering violation.
- Slightly more indirection: a user action on the canvas mutates the model, which
  signals back to the canvas, rather than mutating the widget directly.
