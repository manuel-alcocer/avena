# avena

**A**udio**V**ideo **E**nhanced **N**ode **A**rchitecture — a visual, node-based
transcoding tool for Linux — think *HandBrake*, but where you
**build the conversion pipeline graphically**. You start from one or more input files
and assemble the processing chain (decode → filter → encode → mux → output) by
connecting nodes on a canvas. Under the hood the graph maps to a **GStreamer**
pipeline.

- **UI:** Qt 6 (Widgets, `QGraphicsView` canvas)
- **Language:** C++23
- **Engine:** GStreamer 1.x (programmatic pipeline — built incrementally)
- **Platforms:** Arch Linux, Ubuntu 24.04+, Debian 13+

## Status

Early development. Current milestone: **the node editor**.

- [x] Logical graph model (nodes, typed ports, connections)
- [x] Node catalog (sources, filters, encoders, muxers, sinks)
- [x] Interactive canvas: add / move / connect / delete nodes
- [x] Type-checked connections (video/audio/subtitle compatibility)
- [x] Save / load the graph as JSON
- [x] Dynamic element catalog from the GStreamer registry (incl. libav)
- [x] Media probing + GObject property introspection
- [x] GStreamer execution backend (build + run, with progress and log)
- [ ] Live preview; smarter auto-plugging (parsers/converters beyond common cases)

## Building

Requirements: CMake ≥ 3.21, a C++23 compiler, Qt 6.5+, GStreamer 1.x
(+ base/good/bad/ugly/libav plugins for the full element catalog).

```sh
cmake -B build -G Ninja
cmake --build build
./build/src/avena
```

GStreamer features (the dynamic element catalog, media probing, property
introspection and the execution backend) are **on by default**. If you have an
existing build directory configured with them off, the cached value sticks —
re-enable explicitly:

```sh
cmake -B build -G Ninja -DAVENA_ENABLE_GSTREAMER=ON
```

To build the editor with Qt only (no GStreamer), pass `-DAVENA_ENABLE_GSTREAMER=OFF`.

### Running a pipeline headlessly

`.abk` graphs can be built and executed without the UI:

```sh
./build/src/avena --run pipeline.abk
```

### Distro dependencies

- **Arch:** `sudo pacman -S qt6-base gstreamer gst-plugins-base cmake ninja`
- **Ubuntu/Debian:** `sudo apt install qt6-base-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev cmake ninja-build`

## Project layout

```
src/
  graph/    Logical model: Node, Port, Connection, Graph, NodeCatalog, serializer
  canvas/   QGraphicsView editor: NodeItem, PortItem, ConnectionItem, scene, view
  app/      MainWindow, node palette, properties panel
```

## License

Copyright © 2026 the avena authors — [manuel-alcocer](https://github.com/manuel-alcocer)
and [prietus](https://github.com/prietus).

avena is free software, licensed under the **GNU General Public License v3.0**.
See [LICENSE](LICENSE) for the full text.
