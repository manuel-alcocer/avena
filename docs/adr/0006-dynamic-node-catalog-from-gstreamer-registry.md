# 6. Build the node catalog from the GStreamer registry

- Status: Accepted
- Date: 2026-06-21

## Context

The palette of nodes a user can place (encoders, muxers, filters, sinks) mirrors
the GStreamer elements actually installed on the machine — which varies by distro
and by which plugin sets (base/good/bad/ugly/libav) are present. Hard-coding a
fixed element list would either lie about what is available or omit elements the
user has installed.

## Decision

`NodeCatalog` is a singleton populated at construction. When built with GStreamer,
`populateFromGStreamer()` enumerates the GStreamer registry — including
libav-backed elements — and derives `NodeTypeSpec`s (type id, display/long name,
description, category, ports, default properties) from element metadata. A small
set of built-in **Tools** nodes (File Source, Time Range, Stream Inspector) that
do not map to a single element is added on top.

Per-element editable settings are not baked into the catalog; they are
introspected on demand from each element's `GParamSpec`s
(`ElementProperties::forElement`), so the Properties panel always matches the
installed element.

When built without GStreamer, `populateStatic()` provides a minimal hard-coded set
so the editor still runs — see
[ADR-0007](0007-gstreamer-optional-build-dependency.md).

## Consequences

- The catalog reflects the user's actual machine; newly installed plugins appear
  without code changes.
- Templates are offered only when the encoders they need are installed.
- The catalog is non-deterministic across machines, so a `.abk` graph may
  reference an element the loading machine lacks; loading must degrade rather than
  assume every type resolves.
- Element-property UI is generated from introspection, not maintained by hand.
