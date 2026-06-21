# 5. Centralize connection rules with typed ports

- Status: Accepted
- Date: 2026-06-21

## Context

Not every wiring is valid: you cannot feed an audio stream into a video encoder,
connect a node to itself, or attach two sources to the same input. If the canvas
enforced these rules, the logic would be tangled with mouse handling and could
diverge from what a loaded `.abk` file or a headless run permits.

We also need to carry non-stream parameters (e.g. a bitrate computed by a Stream
Inspector) into a node's settings, which is a different kind of connection from a
media stream.

## Decision

Every `Port` carries a `MediaType` (`Any`, `Video`, `Audio`, `Subtitle`, `Data`,
`Value`). Compatibility is decided by `mediaTypesCompatible(out, in)`, with `Any`
acting as a wildcard on either side.

`Graph::canConnect` is the **single authority** on validity: `from` must be an
Output, `to` an Input, on different nodes, type-compatible, and the input must be
free (an input holds at most one incoming connection). `addConnection` normalizes
endpoint orientation (output→input) and refuses invalid wiring. The canvas calls
`canConnect` for live feedback but does not re-implement the rules.

`MediaType::Value` models **parameter edges**: a scalar value wired into a
destination node's GObject property rather than a media stream. The chosen
property is stored on the `Connection` as `targetProperty`.

## Consequences

- Validity is identical whether a connection is drawn on screen, loaded from a
  file, or built headlessly — one code path, one set of rules.
- Adding a new media kind or compatibility rule is a change in `Types`/`Graph`,
  not scattered across the UI.
- Inputs are single-occupancy; "replace" in the UI is remove-then-add, both via
  `Graph`.
- Parameter wiring reuses the same connection machinery as streams, distinguished
  only by `MediaType::Value` + `targetProperty`, keeping the model uniform.
