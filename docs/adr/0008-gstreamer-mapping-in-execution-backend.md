# 8. Confine the GStreamer mapping to the execution backend

- Status: Accepted (amended by [ADR-0010](0010-time-range-as-file-source-property.md))
- Date: 2026-06-21

## Context

A logical `Graph` is not directly runnable: turning it into a working GStreamer
pipeline requires knowledge that has nothing to do with what the user drew —
decoding sources, inserting format converters, linking dynamic pads, and
requesting muxer pads. If that knowledge leaked into the model or the canvas, the
model would stop being a clean source of truth and headless execution would be
impossible.

## Decision

All graph→GStreamer translation lives in `PipelineRunner` (`src/engine/`). The
runner owns the mapping decisions:

- File Source nodes become `filesrc ! decodebin`, with decodebin's dynamic pads
  linked by media type.
- Encoder inputs get `videoconvert` (or `audioconvert + audioresample`) inserted
  automatically.
- Muxer inputs use request pads.
- Time Range is realized with a pad probe that drops frames outside `[start,
  end]` and sends EOS at the end (or is skipped when `bypass` is set), not as a
  GStreamer element.

The runner surfaces `started` / `progress` / `logMessage` / `finished` signals.
Because it consumes only a `Graph`, the same path powers the UI and the headless
`avena --run pipeline.abk` mode. Live preview of a source file is a separate
concern, handled by `MediaPreview` via `playbin`, independent of the runner.

## Consequences

- The model stays engine-agnostic; auto-plugging logic has exactly one home.
- Headless batch transcoding is a first-class mode, not an afterthought.
- The graph is intentionally higher-level than the pipeline: one node may expand
  to several GStreamer elements, so there is no 1:1 node↔element correspondence to
  rely on.
- Smarter auto-plugging (parsers/converters beyond the common cases) is a future
  change localized to the runner.
