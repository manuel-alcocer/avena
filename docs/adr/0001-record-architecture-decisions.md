# 1. Record architecture decisions

- Status: Accepted
- Date: 2026-06-21

## Context

avena started as a personal prototype (autobrake) and is now a new project with
two authors. A number of architectural choices are already baked into the code
but were never written down, so the reasoning behind them lives only in the
authors' heads and in the shape of the source. As more people (and AI agents)
work on the codebase, that implicit knowledge erodes and gets re-litigated.

## Decision

We record architecturally significant decisions as Architecture Decision Records
(ADRs) under `docs/adr/`, one Markdown file per decision, numbered sequentially
(`NNNN-title.md`). Each ADR uses a lightweight format: **Status**, **Context**,
**Decision**, **Consequences**.

ADRs are immutable once Accepted. To change a decision we write a new ADR that
**supersedes** the old one, rather than editing history. The superseded ADR is
marked as such with a link forward.

ADRs 0002–0009 are **retrospective**: they capture decisions already implemented,
so the codebase has a written reference. New decisions get an ADR before or as
they are implemented.

## Consequences

- The "why" behind the architecture is discoverable and reviewable.
- Skills such as `improve-codebase-architecture`, `diagnose`, and `tdd` read
  `docs/adr/` and can flag changes that contradict a recorded decision.
- A small ongoing cost: significant changes now come with an ADR.
- Domain vocabulary used in ADRs follows `CONTEXT.md`.
