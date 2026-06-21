# 7. Make GStreamer an optional build-time dependency

- Status: Accepted
- Date: 2026-06-21

## Context

GStreamer (and its plugin sets) is a heavy dependency. The node editor itself —
the logical model and the canvas — is useful and testable without it, and
contributors working on the UI should not be forced to have a full media stack
installed. But probing media, the dynamic catalog, property introspection,
preview, and execution all genuinely need GStreamer.

## Decision

GStreamer is gated behind the CMake option `AVENA_ENABLE_GSTREAMER` (on by
default). Every GStreamer-dependent subsystem exposes a static `available()`
predicate — `MediaProbe::available()`, `ElementProperties::available()`,
`PipelineRunner::available()`, `MediaPreview::available()` — and degrades
gracefully when built without it (e.g. the catalog falls back to a static set,
probing returns an invalid `MediaInfo` with a reason).

## Consequences

- The editor builds and runs Qt-only with `-DAVENA_ENABLE_GSTREAMER=OFF`.
- Any feature touching media must check the relevant `available()` and have a
  defined no-GStreamer behaviour; "assume GStreamer is present" is a bug.
- Two build configurations exist to keep working; the static catalog must stay a
  plausible stand-in for the dynamic one.
