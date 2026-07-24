<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0003: Structure-aware, validating carving

- **Status:** Accepted
- **Date:** 2026-07-24

## Context

Signature-only carving (match a magic byte, then collect until a footer / next header /
size cap) is the root cause of the failure that motivates this project: a misdetected or
footer-less header yields an enormous, corrupt file — for example a multi-gigabyte
"SWF" carved from a drive that only ever contained sub-gigabyte photos and videos. The
carver had no model of the file's real length, so it over-collected.

## Decision

Every supported format is recovered by a **validating parser** that walks the format's
internal structure to compute the file's **exact extent**, then checks structural
invariants and attaches a confidence verdict (`Valid` / `Uncertain` / `Rejected`).
Candidates that fail validation are flagged or discarded per configuration — never
written as if they were good files.

A magic-byte match is treated as a *hypothesis* that triggers validation, not as a
recovered file. There is no "collect until size cap" fallback path that can emit
unbounded garbage.

## Consequences

- Precision improves dramatically for the target formats; the "1 GB SWF" class of false
  positive is eliminated by construction.
- Each format costs more up-front: a real parser plus mandatory unit and fuzz tests,
  rather than a magic string. This is the core value of the project and is accepted.
- Parsers must be **total** (any input → verdict or typed error, never a crash), which
  raises the testing bar; fuzzing every parser is therefore a merge gate.
- Formats we have not yet written a validator for are simply unsupported until added via
  `revenant:add-format-carver`. We do not ship weak signature-only carvers as filler.
