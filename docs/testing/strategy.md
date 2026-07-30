<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Testing Strategy

Revenant parses hostile, corrupt bytes for a living. Testing is therefore not a
formality — it is the primary defense against the crashes, hangs, and silent corruption
that a recovery tool must never exhibit. **No production code lands without a test.**

## Test-driven by default

Write the failing test first (red), make it pass (green), then refactor. This is the
default workflow, not an aspiration. It keeps functions small and forces testable seams.

## The test pyramid

```
        ▲   fuzz         every byte-parser: hostile inputs, no crash/hang/OOB
       ▲▲   golden-file  bit-exact recovered output vs. known-good originals
      ▲▲▲   integration  synthetic disk images through real layers
     ▲▲▲▲   unit         hand-crafted byte fixtures, one behaviour each
```

### Unit tests (the base)

- One behaviour per test; fast, deterministic, no privileges.
- Byte parsers are tested with **hand-crafted fixtures**: a valid case, and then
  truncated, malformed, and boundary cases for every parser.
- Backed by `InMemoryDevice` — no filesystem or device access required.

### Integration tests

- Exercise real layers against **small synthetic disk images** produced by `tools/`
  (checked-in, deterministic). An NTFS image with known deleted files flows through the
  device → filesystem → recovery layers.
- Assert on recovered names, paths, timestamps, and byte content.

### Golden-file tests

- For carving: embed a known-good file in a larger buffer, carve it, and assert the
  output is **byte-identical** to the original. This is the anti-false-positive gate —
  it catches over- and under-collection directly.

### Fuzz tests (the apex)

- Every parser that consumes external bytes (carvers, MFT/attribute/runlist parsers,
  directory-entry parsers, `ByteReader`) has a **libFuzzer** target.
- Invariant: any input produces a verdict or a typed error — never a crash, hang,
  unbounded loop, or out-of-bounds access.
- Fuzz targets run briefly in CI on every PR (smoke) and for longer on a schedule.
  Discovered crashers are committed as regression corpus entries.

## Fixtures & tools

- `tests/fixtures/` holds small, checked-in, deterministic images and byte samples.
- `tools/` holds generators that build these images reproducibly, so fixtures can be
  regenerated and reviewed rather than being opaque binaries.
- Generated (non-committed) images go under `tests/fixtures/generated/` (git-ignored).

## Determinism & isolation

- Tests must be deterministic: no reliance on wall-clock time, ordering, or real
  devices. Time and randomness, where needed, are injected.
- Tests never touch real disks. Physical-device code is validated against a
  fault-injecting fake `BlockDevice`.

## What "tested" means for a story

A story is not Done until its acceptance criteria map to passing tests at the
appropriate pyramid levels, coverage holds or rises, against the floor in
  [quality-gates.md](quality-gates.md), and every new
byte-parser has a fuzz target. See [quality-gates.md](quality-gates.md).
