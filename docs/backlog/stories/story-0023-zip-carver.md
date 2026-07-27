<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0023: ZIP-based carver (EOCD; DOCX/XLSX/PPTX classification)

- Epic: [epic-m2-carving-breadth](../epic-m2-carving-breadth.md)
- Status: Done
- Size: M

## Goal

Recover ZIP archives — and the Office documents that are ZIP archives — by
locating and *validating* the End Of Central Directory record, which is what
makes the extent exact rather than a search for a plausible tail.

## Design references

- [ADR-0003](../../architecture/adr/adr-0003-validating-carving.md).
- [Carving engine](../../architecture/carving-engine.md) — "Locate the
  End-Of-Central-Directory record; derive extent."

## Signature & extent

- **Signature:** `50 4B 03 04` (`PK\x03\x04`, a local file header) at offset 0.
- **Exact extent:** the End Of Central Directory record (`PK\x05\x06`) is the
  archive's last structure. Its 22-byte body carries the central directory's
  size and offset and a trailing comment length, so the extent is
  `eocdOffset + 22 + commentLength`.
- **What makes it validating:** the EOCD is not merely *found*, it is *checked*.
  A real archive satisfies `centralDirectoryOffset + centralDirectorySize ==
  eocdOffset`, and the bytes at that offset begin a central directory header
  (`PK\x01\x02`). A `PK\x05\x06` that fails those checks is a coincidence in the
  data, not the end of this archive.
- **Verdicts:**
  - *Rejected* — no local file header at offset 0, or no EOCD at all: there is
    no extent to report.
  - *Valid* — an EOCD whose central-directory arithmetic and header check both
    hold.
  - *Uncertain* — an EOCD was found but did not validate, or its comment runs
    past the available bytes; the extent is bounded by what is there.
- **Extension:** from the names in the central directory — `word/` → `docx`,
  `xl/` → `xlsx`, `ppt/` → `pptx`, otherwise `zip`.

## Acceptance criteria

- [x] `ZipCarver` implements `FormatCarver`; `signatures()` returns the local
      file header magic at offset 0.
- [x] Locating and validating the EOCD lives in its own unit.
- [x] The **last** EOCD candidate in the data is used, so an archive containing
      another archive's bytes does not stop early.
- [x] A comment length running past the available bytes bounds the extent.
- [x] Central-directory name scanning is bounded by a named constant (ADR-0009)
      and by the bytes available.
- [x] `ZipCarver` is registered in `registerBuiltinCarvers`.
- [x] A libFuzzer target `ZipCarverFuzz` is wired and must never crash.

## Test plan

Unit (`tests/unit/carve/formats/ZipCarverTest.cpp`):

- a minimal one-entry archive → exact length, `Valid`, `zip`;
- trailing garbage after the EOCD → extent stops at the EOCD;
- an archive whose entry names start with `word/`, `xl/`, `ppt/` → `docx`,
  `xlsx`, `pptx`;
- an EOCD whose central-directory offset does not add up → `Uncertain`;
- a stray `PK\x05\x06` inside the data followed by the real EOCD → the real one
  wins;
- a comment length past the end → `Uncertain`, bounded;
- no EOCD → `Rejected`;
- non-ZIP bytes → `Rejected`.

Fuzz (`tests/fuzz/ZipCarverFuzz.cpp`): arbitrary bytes into `carve`.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan+UBSan).
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `docs/architecture/carving-engine.md` updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
