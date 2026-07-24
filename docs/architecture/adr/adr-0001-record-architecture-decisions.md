<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0001: Record architecture decisions

- **Status:** Accepted
- **Date:** 2026-07-24

## Context

Revenant is intended to be a long-lived, enterprise-grade project. Significant design
choices need a durable, reviewable record so that future contributors (human or AI)
understand *why* the system is shaped the way it is, not just *what* it does.

## Decision

We use lightweight **Architecture Decision Records** (ADRs), one Markdown file per
decision, stored in `docs/architecture/adr/` and numbered sequentially
(`adr-NNNN-title.md`, lowercase). Each ADR has: Status, Date, Context, Decision,
Consequences. ADRs are immutable once Accepted; a later ADR supersedes an earlier one
rather than editing it.

Any decision that is hard to reverse, affects a public interface, or would surprise a
newcomer must be captured as an ADR. The milestone-level architecture audit reviews
whether reality still matches the accepted ADRs.

## Consequences

- Decisions are traceable and challengeable.
- A small overhead per significant decision — acceptable and intended.
- The ADR index grows into a design history of the project.
