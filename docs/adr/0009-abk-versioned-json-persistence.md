# 9. Persist graphs as a versioned JSON .abk document

- Status: Accepted
- Date: 2026-06-21

## Context

Users need to save and reload their pipelines, share them, and run them
headlessly. The format is a long-lived contract: files saved today must keep
loading as the schema evolves. The on-disk form should also be diff-friendly and
inspectable, since templates ship as files and are sometimes hand-edited.

## Decision

A `Graph` serializes to a small, stable JSON document handled by
`GraphSerializer`:

```json
{ "version": 1, "nodes": [...], "connections": [...] }
```

The file extension is **`.abk`** (a deliberate carry-over from the project's
autobrake origin). Nodes and connections are stored in terms of stable ids and
`PortRef` indices ([ADR-0004](0004-stable-portref-addressing.md)), and media
types are written with stable string identifiers (`mediaTypeId`) rather than
enum ordinals. The top-level `version` field gates schema evolution. Loading is
defensive: a malformed document fails with an `error` rather than producing a
half-built graph.

The same format backs user-saved templates.

## Consequences

- Saved graphs are human-readable, diffable, and editable by tooling (templates
  are tweaked with `sed` in practice).
- Stable string ids and the `version` field let the schema grow without breaking
  old files; readers must tolerate unknown/missing optional fields per version.
- Because the catalog is machine-dependent
  ([ADR-0006](0006-dynamic-node-catalog-from-gstreamer-registry.md)), a loaded
  file may reference a node type the current machine lacks; deserialization must
  handle unresolved types gracefully.
