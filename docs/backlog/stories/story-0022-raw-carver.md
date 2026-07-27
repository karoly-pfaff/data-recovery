<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0022: RAW carver (TIFF/IFD: CR2, NEF, ARW)

- Epic: [epic-m2-carving-breadth](../epic-m2-carving-breadth.md)
- Status: Done
- Size: L

## Goal

Recover camera RAW files — which are TIFF containers — by walking the IFD chain
and computing the exact extent from where the metadata and the image data
actually end, and name them by the camera that wrote them.

## Design references

- [ADR-0003](../../architecture/adr/adr-0003-validating-carving.md).
- [Carving engine](../../architecture/carving-engine.md) — "Parse the TIFF header
  and IFD chain."

## Signature & extent

- **Signatures:** `49 49 2A 00` (little-endian TIFF) and `4D 4D 00 2A`
  (big-endian), both at offset 0. Byte order is not a detail here — every field
  in the file is read in it.
- **Structure:** the 8-byte header points at the first IFD. An IFD is an entry
  count (2 bytes), that many 12-byte entries (tag, type, count, value-or-offset),
  and a 4-byte offset to the next IFD; 0 ends the chain.
- **Exact extent:** the maximum end offset over everything the file references —
  each IFD table, every entry value too large to sit inline in its 4 bytes, and
  the image data located by `StripOffsets`/`StripByteCounts` (0x0111/0x0117) or
  `TileOffsets`/`TileByteCounts` (0x0144/0x0145). The image data is normally the
  last and largest thing in the file, which is what makes this exact rather
  than a guess.
- **Verdicts:**
  - *Rejected* — the header is not a TIFF header, or no IFD could be read
    at all: there is nothing to recover either way.
  - *Valid* — at least one IFD parsed, image data located, and everything the
    file points at lies inside the bytes available.
  - *Uncertain* — the header parsed but the chain broke, no image data was
    located, or the computed extent runs past the available bytes; the reported
    extent is then bounded by what is actually there.
- **Extension:** `cr2` when the header carries Canon's `CR` marker at offset 8;
  otherwise from the `Make` tag (0x010F) — `nef` for Nikon, `arw` for Sony,
  `tif` for anything else.

## Bounded traversal

An IFD's `next` pointer can point backwards, so the chain can be made to loop.
The walk therefore caps the chain at a named constant (ADR-0009) rather than
trusting the file to terminate, and every array element count is bounded by the
bytes actually available before it sizes a loop.

## Acceptance criteria

- [x] `RawCarver` implements `FormatCarver` with both byte-order signatures.
- [x] Field reads honour the file's byte order throughout.
- [x] Entry decoding lives in its own unit: type sizes, inline vs out-of-line
      values, and array element access.
- [x] The IFD walk lives in its own unit and computes the extent as described.
- [x] A cyclic or over-long IFD chain terminates at the cap instead of hanging.
- [x] An entry whose value or array runs past the available bytes bounds the
      extent instead of reading out of range.
- [x] The extension follows the CR2 marker and the `Make` tag.
- [x] `RawCarver` is registered in `registerBuiltinCarvers`.
- [x] A libFuzzer target `RawCarverFuzz` is wired and must never crash.

## Test plan

Unit (`tests/unit/carve/formats/RawCarverTest.cpp`):

- a little-endian TIFF with `Make`, strips and image data → exact length,
  `Valid`;
- the same in big-endian → identical result;
- trailing garbage after the image data → extent stops at the image data;
- Canon's `CR` header marker → `cr2`; `NIKON` → `nef`; `SONY` → `arw`; an
  unknown make → `tif`;
- tile tags instead of strip tags → located the same way;
- a self-referencing IFD chain → terminates, `Uncertain`;
- a strip offset pointing past the data → `Uncertain`, bounded;
- an IFD with no image-data tags → `Uncertain`;
- non-TIFF bytes → `Rejected`.

Fuzz (`tests/fuzz/RawCarverFuzz.cpp`): arbitrary bytes into `carve`.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan+UBSan).
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `docs/architecture/carving-engine.md` updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
