<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0201: PNG validating carver (chunk walk + CRC-32)

- Epic: [epic-m2-carving-breadth](../epic-m2-carving-breadth.md)
- Status: Done
- Size: M

## Goal

Recover PNG files by walking their chunk list to the exact byte after `IEND`,
verifying every chunk's CRC-32 — the second validating carver, and the one that
proves the engine generalizes past JPEG's marker walk.

## Design references

- [ADR-0003](../../architecture/adr/adr-0003-validating-carving.md) — validating
  carving: never "collect bytes until a footer".
- [Carving engine](../../architecture/carving-engine.md) — "Walk the chunk list
  IHDR → … → IEND, verifying each chunk's CRC-32."

## Signature & extent

- **Signature:** `89 50 4E 47 0D 0A 1A 0A` at offset 0 (8 bytes).
- **Structure:** after the signature, a sequence of chunks, each
  `length` (BE32) `type` (4 bytes) `data` (`length` bytes) `crc` (BE32).
  The CRC covers `type` + `data`, which are contiguous — one span, no copy.
- **Exact extent:** the offset just past the `IEND` chunk's CRC.
- **Verdicts:**
  - *Rejected* — signature absent, or the first chunk is not `IHDR`.
  - *Valid* — `IHDR` first, every CRC verified, `IEND` reached.
  - *Uncertain* — `IHDR` seen but the walk stopped early (truncation, a chunk
    length running past the data, or a CRC mismatch); the extent is the end of
    the last chunk that verified.

CRC-32 (IEEE 802.3) arrives as a shared core utility because the ZIP carver
([story-0204](story-0204-zip-carver.md)) needs the same polynomial.

## Acceptance criteria

- [x] `revenant::crc32(std::span<const std::byte>)` computes IEEE 802.3 CRC-32
      and matches published test vectors.
- [x] `PngCarver` implements `FormatCarver`: `signatures()` returns the 8-byte
      magic at offset 0; `carve()` returns the exact extent and verdict.
- [x] The chunk walk lives in its own unit, not inside `carve()`.
- [x] A chunk length that would run past the available bytes stops the walk
      instead of reading out of range.
- [x] `PngCarver` is registered in `registerBuiltinCarvers`.
- [x] A libFuzzer target `PngCarverFuzz` is wired and must never crash.

## Test plan

Unit (`tests/unit/core/Crc32Test.cpp`): the published vectors — empty input,
`"123456789"` → `0xCBF43926` — plus a single byte, so the fixture builder below
rests on an independently verified primitive.

Unit (`tests/unit/carve/formats/PngCarverTest.cpp`):

- a valid minimal PNG → exact length and `Valid`;
- the same followed by trailing garbage (including a second PNG signature) →
  extent stops at `IEND` (the anti-false-positive case);
- truncated mid-chunk → `Uncertain`, extent bounded at the last good chunk;
- a corrupted CRC → `Uncertain`, extent bounded before the bad chunk;
- a first chunk that is not `IHDR` → `Rejected`;
- an absurd chunk length → `Uncertain`, no overrun;
- non-PNG bytes → `Rejected`.

Fuzz (`tests/fuzz/PngCarverFuzz.cpp`): arbitrary bytes into `carve`.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan+UBSan).
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `docs/architecture/carving-engine.md` updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
