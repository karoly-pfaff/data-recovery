<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0010: JPEG validating carver

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Ready
- Size: M

## Goal

Deliver the first `FormatCarver` and, with it, prove the validating-carving thesis end
to end: a JPEG is recovered at its **exact** extent by walking its marker structure —
never by collecting bytes to a size cap. This is the direct answer to the "1 GB SWF"
false positive.

## Design references

- [Carving engine](../../architecture/carving-engine.md)
- [ADR-0003: validating carving](../../architecture/adr/adr-0003-validating-carving.md)

## Acceptance criteria

- [ ] `JpegCarver` implements `FormatCarver` in `src/carve/formats/JpegCarver.{hpp,cpp}`.
- [ ] Signature: `FF D8 FF` (SOI). Extension: `jpg`.
- [ ] `carve` walks markers from SOI, correctly skipping segments by their length and
      scanning entropy-coded data (handling `FF 00` byte-stuffing and RST markers), and
      returns the exact offset just past `FF D9` (EOI).
- [ ] Returns `Valid` for a well-formed JPEG, `Uncertain` for a plausible-but-imperfect
      one, and `Rejected` for bytes that are not a JPEG.
- [ ] Never reads past the provided reader bounds; never loops unboundedly on malformed
      input.
- [ ] Registered in `CarverRegistry`.

## Test plan

- Unit: minimal valid baseline JPEG → exact length, `Valid`.
- Unit: JPEG followed by trailing garbage → extent stops at EOI (the core anti-false-
  positive test).
- Unit: truncated JPEG (no EOI) → bounded result, `Uncertain`/`Rejected`, no overrun.
- Unit: byte-stuffing and RST markers within entropy data handled correctly.
- Golden-file: carve a JPEG embedded in a larger buffer; output is byte-identical to the
  original.
- **Fuzz:** libFuzzer target over `carve`; any input yields a verdict or typed error,
  never a crash, hang, or OOB read. (Merge gate.)

## Definition of Done

- [ ] Acceptance criteria met; unit + golden + fuzz tests green under ASan + UBSan.
- [ ] `JpegCarver.cpp` ≤ 250 lines; each function ≤ 10 statements and does one thing
      (marker walk, segment skip, entropy scan, and validation are separate functions).
- [ ] Coverage held or raised; lint/format/duplication/file-length guards clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Story-level self-audit checklist completed.
