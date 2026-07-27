<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0021: MP4/MOV validating carver (box tree)

- Epic: [epic-m2-carving-breadth](../epic-m2-carving-breadth.md)
- Status: Done
- Size: L

## Goal

Recover ISO base media files (MP4, MOV, M4A/M4V) by walking the top-level box
list and summing box sizes to the exact extent, and distinguish QuickTime from
MP4 by the `ftyp` major brand so the recovered file gets the right extension.

## Design references

- [ADR-0003](../../architecture/adr/adr-0003-validating-carving.md).
- [Carving engine](../../architecture/carving-engine.md) — "Walk the atom/box
  tree (`ftyp`, `moov`, `mdat`), summing box sizes."

## Signature & extent

- **Signature:** `ftyp` at **offset 4** — the first non-zero signature offset in
  the engine (see the scanner fix below).
- **Structure:** top-level boxes, each `size` (BE32) + `type` (4 bytes). A size
  of `1` means the real size follows as a BE64 `largesize`; a size of `0` means
  "to end of file", which for a carve candidate is precisely *unknown*.
- **Exact extent:** the sum of the top-level box sizes walked from offset 0.
- **Verdicts:**
  - *Rejected* — the first box is not `ftyp`.
  - *Valid* — `ftyp`, `moov` and `mdat` all seen at top level.
  - *Uncertain* — `ftyp` seen but the walk stopped before both `moov` and
    `mdat`: a truncation, a size below the 8-byte header, a size running past
    the data, a size-0 box, or a box type that is not four printable ASCII
    characters. The extent is the end of the last box that parsed.
- **Extension:** `mov` when the `ftyp` major brand is `qt  `, else `mp4`.

## Scanner fix this story forces

`WindowMatch` computed a candidate's start as `windowOffset + at -
signature.offset` in unsigned arithmetic. With every signature at offset 0 that
was safe; with `ftyp` at offset 4, a magic found in the first four bytes of the
device wraps to an address near 2^64 and invents a candidate there. The start is
now computed through a checked helper that drops such a match, and the window
plus its device offset travel together as one `Window` value instead of two
parallel parameters.

## Acceptance criteria

- [x] `Mp4Carver` implements `FormatCarver`; `signatures()` returns `ftyp` at
      offset 4.
- [x] The box walk lives in its own unit and handles the 32-bit size, the
      64-bit `largesize` form, and rejects a size below the header length.
- [x] A box size that would run past the available bytes stops the walk instead
      of reading out of range; a size-0 box stops it too, because its extent
      cannot be known.
- [x] A box type that is not four printable ASCII characters stops the walk —
      this is what keeps trailing garbage from being absorbed.
- [x] The verdict follows the `ftyp`/`moov`/`mdat` rules above and the extension
      follows the major brand.
- [x] `Mp4Carver` is registered in `registerBuiltinCarvers`.
- [x] A magic found closer to the device start than its own signature offset no
      longer produces a wrapped candidate offset.
- [x] A libFuzzer target `Mp4CarverFuzz` is wired and must never crash.

## Test plan

Unit (`tests/unit/carve/formats/Mp4CarverTest.cpp`):

- a valid `ftyp`+`moov`+`mdat` file → exact length, `Valid`, `mp4`;
- the `qt  ` major brand → `mov`;
- trailing garbage after the last box → extent stops at the last box;
- a truncated `mdat` → `Uncertain`, bounded;
- a 64-bit `largesize` box → walked correctly;
- a size-0 box → `Uncertain`, bounded before it;
- a size below 8 → `Uncertain`, bounded;
- a non-printable box type → `Uncertain`, bounded;
- bytes without `ftyp` first → `Rejected`.

Unit (`tests/unit/carve/SignatureScannerTest.cpp`): a magic whose in-file offset
reaches before the device start yields no candidate rather than a wrapped one.

Fuzz (`tests/fuzz/Mp4CarverFuzz.cpp`): arbitrary bytes into `carve`.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan+UBSan).
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `docs/architecture/carving-engine.md` updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
