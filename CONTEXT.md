# CONTEXT — avena

**avena** (AudioVideo Enhanced Node Architecture) is a visual, node-based
transcoding tool for Linux. The user assembles a processing chain
(decode → filter → encode → mux → output) by wiring nodes on a canvas; the
chain is then mapped to a **GStreamer** pipeline and executed.

This document defines the project's domain language. When naming a concept in
code, an issue, a test, or a proposal, use the term as defined here and don't
drift to synonyms.

avena is a new project (two authors: manuel-alcocer and prietus). It started life
as a personal prototype called **autobrake** — that heritage survives in two
identifiers that are *not* renamed: the C++ namespace `ab` for the whole codebase,
and the `.abk` graph file extension. Treat "autobrake" as historical; the project
is **avena**.

## The three layers

avena deliberately separates three representations of "the pipeline". Keep them
distinct — conflating them is the most common source of confusion.

1. **Logical model** (`src/graph/`) — passive data objects describing *what* the
   pipeline is: `Graph`, `Node`, `Port`, `Connection`. No Qt widgets, no
   GStreamer. This is the source of truth and the thing that gets serialized.
2. **Canvas** (`src/canvas/`) — the interactive `QGraphicsView` editor: `NodeItem`,
   `PortItem`, `ConnectionItem`, `NodeEditorScene`, `NodeEditorView`. These are
   *visual representations* that observe the logical model via raw pointers and
   react to its signals. They own no domain state.
3. **Execution backend** (`src/engine/`) — `PipelineRunner` translates the logical
   `Graph` into a concrete GStreamer pipeline and runs it. The mapping
   (catalog type → GStreamer element, auto-inserted converters, pad linking)
   lives here, not in the model.

`Graph` owns its `Node`s and `Connection`s exclusively (`unique_ptr`) and emits
change signals; the canvas listens. Ownership never flows the other way.

## Glossary

### Logical model

- **Graph** — the logical pipeline graph. Owns nodes and connections, enforces
  connection rules (`canConnect`), and emits change signals. The unit of
  save/load.
- **Node** — one vertex of the graph. A passive data object with a stable, unique
  `id`, a catalog `typeId` (e.g. `"x264enc"`), a `category`, `inputs`/`outputs`
  ports, and a free-form `properties` map (`QVariantMap`). The node does *not*
  know about its visual item or its GStreamer element.
- **Port** — a single connection point on a node, carrying a `MediaType`. Ports
  are addressed by their **index** within the node's `inputs` or `outputs` list.
- **PortDirection** — `Input` or `Output`, relative to the owning node.
- **MediaType** — the kind of thing flowing through a port:
  `Any`, `Video`, `Audio`, `Subtitle`, `Data`, `Value`. `Any` is a wildcard for
  compatibility checks. `Value` is special (see below).
- **Value edge** (parameter edge) — a connection whose `MediaType` is `Value`. It
  carries a scalar parameter (e.g. a computed bitrate) into a destination node's
  GObject **property** rather than a media stream. The chosen property is stored
  on the connection as `targetProperty`.
- **PortRef** — a stable `{nodeId, direction, index}` reference to a specific port.
  Connections are expressed in terms of `PortRef` so they survive serialization
  and never dangle on raw pointers.
- **Connection** — a directed edge from an output port (`from`) to an input port
  (`to`). Always normalized output→input. An input port may hold at most one
  incoming connection ("occupied").

### Catalog & templates

- **NodeCatalog** — the singleton registry of all available node types. Built
  from the GStreamer registry (including libav-backed elements) when compiled
  with GStreamer, or from a minimal static set otherwise.
- **NodeTypeSpec** — a template describing a node type: `typeId`, display/long
  names, `category`, port specs, and `defaultProperties`. Concrete `Node`s are
  instantiated from a spec; each spec maps to one or more GStreamer elements.
- **PortSpec** — the port description on a `NodeTypeSpec` (name + `MediaType`).
- **Category** — the catalog grouping a node belongs to (Sources, Filters,
  Encoders, Muxers, Sinks, and **Tools** — built-in nodes that don't map to a
  single GStreamer element).
- **Template** (`TemplateInfo` / `TemplateLibrary`) — a named, pre-built pipeline
  the user drops onto an empty canvas (e.g. H.264/MKV/WebM/MP3). A template is
  only offered when the encoders it needs are actually installed. Users can save
  their own templates as `.abk` files.

### Nodes with special backend handling

A few node types don't map straightforwardly to a single GStreamer element; the
execution backend gives them special treatment.

- **File Source** (`source.file`) — a source node holding a `location` property.
  Setting its location **probes** the media and rebuilds its output ports to
  match the file's streams (video/audio/subtitle), dropping any now-invalid
  outgoing connections. It also carries `start` / `end` properties that **trim**
  decoding to a `[start, end]` time window applied to all of this source's
  streams (empty = no trim, the whole file). Trimming is a property of the source
  because it is enforced at its decoded pads — see
  [ADR-0010](docs/adr/0010-time-range-as-file-source-property.md).
- **Stream Inspector** (`tool.inspector`) — the lone node in the **Tools**
  category. Passes a stream through unchanged and exposes its properties
  (bitrate, resolution, fps, channels…) as `Value` outputs you can wire into
  other nodes' aux inputs. The available outputs follow the connected stream
  type, and are rebuilt when its enable flags or input connection change.

### GStreamer side

- **Element** — a GStreamer element factory (e.g. `x264enc`, `filesrc`). A node
  type maps to one or more elements; the runner creates them at build time.
- **ElementProperty** — a writable GObject property of an element, introspected
  from its `GParamSpec` so the UI can build the right editor (bool/int/double/
  string/enum). Drives the **Properties panel**.
- **Pipeline** — the live GStreamer pipeline built from the graph by
  `PipelineRunner`. File Sources become `filesrc ! decodebin` (dynamic pads
  linked by media type); encoder inputs get `videoconvert` /
  `audioconvert + audioresample` inserted automatically; muxer inputs use
  request pads.
- **MediaInfo / MediaProbe** — the result and the act of analysing a media file:
  container, duration, file size, and a list of `MediaStreamInfo` (per
  elementary stream: codec, bitrate, dimensions, framerate, channels, etc.).
- **MediaPreview** — independent `playbin`-based playback of a File Source's input
  file in its own window. Not part of the transcoding pipeline.

### Persistence

- **`.abk` file** — the on-disk form of a `Graph`: a small, stable JSON schema
  `{ "version": 1, "nodes": [...], "connections": [...] }`, written by
  `GraphSerializer`. Graphs can be built and run headlessly with
  `avena --run pipeline.abk`.

## Invariants worth respecting

- The **logical model is the source of truth**. Canvas items and GStreamer
  elements are derived; never store domain state only in a `NodeItem` or an
  element.
- A connection is valid only when: `from` is an Output, `to` is an Input, they
  are on different nodes, their media types are compatible, and the input is
  free. `Graph::canConnect` is the single authority — go through it.
- Ports are referenced by **index**, not pointer. Rebuilding a node's ports (e.g.
  re-probing a File Source) can invalidate existing connections by design; the
  `Graph` drops the now-invalid ones and signals the change.
- GStreamer is **optional at build time** (`AVENA_ENABLE_GSTREAMER`). Anything
  touching probing, the dynamic catalog, property introspection, preview, or
  execution must degrade gracefully (check the corresponding `available()`).

## Terms to avoid

- Don't call a logical `Node` a "widget" or an "item" — those are the canvas's
  `NodeItem`. Likewise, "edge"/"link" should be **Connection**; "socket"/"pin"
  should be **Port**.
- Don't say "GStreamer node". Nodes are logical; GStreamer has **elements**. The
  bridge between them is the **execution backend**.
