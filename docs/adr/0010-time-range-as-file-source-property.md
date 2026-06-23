# 10. Model the time range as a File Source property, not a node

- Status: Accepted
- Date: 2026-06-23
- Amends: [ADR-0008](0008-gstreamer-mapping-in-execution-backend.md)

## Context

Time-range trimming was modelled as a standalone **Time Range** node
(`tool.timerange`) with a single `Any → Any` input/output pair. Two problems:

1. **The wiring was a lie.** The node's ports suggested you route a stream
   through it to trim that stream. In reality the runner ignored the wiring
   entirely: it scanned the graph for the first non-bypassed Time Range node and
   applied its window globally, as a pad probe on *every* decoded pad of *every*
   File Source. The single `Any` port also left the user unsure whether to wire
   the video, the audio, or "the whole thing" — a question with no real answer,
   since the effect was global regardless.

2. **It misplaced the concept.** Trimming is enforced at a source's decoded pads
   (drop frames before `start`, send EOS at `end` so the pipeline drains without
   a seek). That makes it a property of the *source*, not a link in the chain.

## Decision

Remove the `tool.timerange` node. Add `start` and `end` properties to the
**File Source** (`source.file`) node. The runner reads each File Source's own
`start`/`end` and installs the trimming pad probe on that source's decoded pads.

- Empty `start`/`end` means no trim (the whole file) — so the old `bypass` toggle
  is gone; clearing the fields is the bypass.
- Trimming is now **per source**: different sources can have different windows,
  which the previous global model could not express.
- The probe mechanism itself is unchanged (seek-free, keeps muxer output valid);
  only where the window comes from has moved.

## Consequences

- The graph is honest: there is no node whose wiring is ignored, and no ambiguous
  `Any` port. The time window lives where it takes effect.
- The Properties panel renders `start`/`end` for a File Source automatically (via
  the raw-property path), with no custom editor.
- Per-source trimming is newly possible; the single-global-window behaviour is
  gone (a deliberate gain, not a regression).
- `.abk` files containing a `tool.timerange` node from before this change no
  longer resolve that type on load; such graphs must be re-authored (acceptable
  at this stage of development).
- This amends ADR-0008: the time range is still realized by a decoder pad probe
  rather than a GStreamer element, but it is no longer a node in the graph.
