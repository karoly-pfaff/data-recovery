<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0024: PDF validating carver (`%PDF` … `%%EOF`, xref)

- Epic: [epic-m2-carving-breadth](../epic-m2-carving-breadth.md)
- Status: Done
- Size: M

## Goal

Recover PDFs by ending them at their real trailer rather than at the first
`%%EOF` that happens to appear — an incrementally saved PDF contains several,
and only the last one ends the file.

## Design references

- [ADR-0003](../../architecture/adr/adr-0003-validating-carving.md).
- [Carving engine](../../architecture/carving-engine.md) — "Match `%PDF` … final
  `%%EOF`, validating the xref/trailer."

## Signature & extent

- **Signature:** `%PDF-` at offset 0.
- **Exact extent:** the **last** `%%EOF` marker, plus the end-of-line bytes that
  follow it. A PDF saved incrementally carries one `%%EOF` per revision, so the
  first is the end of the *oldest* revision, not of the file.
- **What makes it validating:** every revision ends `startxref`, a byte offset,
  then `%%EOF`. The offset is read and checked: it must land inside the file and
  point at either a classic `xref` table or an indirect object header (a cross
  reference stream, `N G obj`). A `%%EOF` with no usable `startxref` before it is
  a string in the data, not a trailer.
- **Verdicts:**
  - *Rejected* — no `%PDF-` header, or no `%%EOF` at all: there is no extent to
    report.
  - *Valid* — the last `%%EOF` is backed by a `startxref` whose offset resolves.
  - *Uncertain* — an `%%EOF` was found but its `startxref` is missing or points
    nowhere usable; the extent is still exact at the marker, but the file is not
    vouched for.
- **Extension:** `pdf`.

## Acceptance criteria

- [x] `PdfCarver` implements `FormatCarver`; `signatures()` returns `%PDF-` at
      offset 0.
- [x] Locating the trailer lives in its own unit.
- [x] The last `%%EOF` is used, not the first.
- [x] The extent includes the end-of-line bytes that follow `%%EOF` when present
      (`\r`, `\n`, or `\r\n`) and nothing more.
- [x] The `startxref` offset is parsed and resolved against the file's own bytes.
- [x] The digits after `startxref` are bounded before they are parsed (ADR-0009).
- [x] `PdfCarver` is registered in `registerBuiltinCarvers`.
- [x] A libFuzzer target `PdfCarverFuzz` is wired and must never crash.

## Test plan

Unit (`tests/unit/carve/formats/PdfCarverTest.cpp`):

- a minimal one-revision PDF → exact length, `Valid`;
- trailing garbage after `%%EOF` → extent stops at the marker;
- an incrementally saved PDF with two `%%EOF`s → extent reaches the second;
- a trailing `\r\n` after `%%EOF` → included in the extent;
- a `startxref` offset pointing past the file → `Uncertain`, extent still exact;
- a `%%EOF` with no `startxref` before it → `Uncertain`;
- a cross-reference stream (`12 0 obj`) instead of an `xref` table → `Valid`;
- a `%PDF-` header with no `%%EOF` → `Rejected`;
- non-PDF bytes → `Rejected`.

Fuzz (`tests/fuzz/PdfCarverFuzz.cpp`): arbitrary bytes into `carve`.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan+UBSan).
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `docs/architecture/carving-engine.md` updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
