<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0002: Two frontends over a shared core

- **Status:** Accepted
- **Date:** 2026-07-24

## Context

The problem has two distinct user-facing jobs: filesystem-aware undelete (names, paths,
timestamps) and filesystem-independent carving. PhotoRec and TestDisk solve these as two
separate programs, and users are familiar with that split. Both jobs share substantial
lower-level machinery: device I/O, byte utilities, error handling, and — for hybrid
mode — the carve engine itself.

## Decision

Build a shared static core library **`librevenant`** containing all layers (core, I/O,
volume, filesystems, carve engine, recovery orchestration), and **two thin executables**
on top:

- `revenant-carve` — carve-only frontend.
- `revenant-undelete` — filesystem-aware frontend, with `--hybrid` invoking the carve
  engine over unallocated space.

The executables contain only argument parsing, configuration mapping, and progress/UX.
All logic lives in the library and is tested independently of the CLIs.

## Consequences

- Clear separation of concerns; each binary has a focused purpose users recognize.
- The carve engine is reused by both, so hybrid mode adds no duplicate logic (DRY).
- Slightly more build wiring than a single monolith — acceptable.
- A future GUI or scripting binding is just another frontend over the same core.
