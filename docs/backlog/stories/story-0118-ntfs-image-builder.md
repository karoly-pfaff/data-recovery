<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0118: NTFS synthetic-image builder in `tools/imagegen`

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: L

## Goal

Generate the deterministic NTFS test image the rest of M1 is verified against: a
volume whose boot sector, `$MFT`, directory tree, and file data are real enough
for the production parsers to walk, containing live, deleted, and orphaned files
plus a JPEG that exists only in unallocated space.

## Design references

- [Filesystem layer](../../architecture/filesystems.md) — the synthetic-image
  testing strategy ("small synthetic images produced by `tools/`, deterministic,
  checked-in fixtures, covering live, deleted, and orphaned entries").
- [story-0007](story-0007-imagegen-scaffold.md) — the `revenant-imagegen`
  scaffold this extends.

## Scope

A **fixed** synthetic volume, not a general-purpose NTFS writer. Nothing here is
parameterized beyond the output path: YAGNI, and a fixture that changes shape is
a fixture no test can assert against.

New units under `tools/imagegen/ntfs/`, each one responsibility:

| Unit | Responsibility |
|------|----------------|
| `NtfsLayout` | The volume plan: geometry, `$MFT` location, data-area start |
| `BootSectorBuilder` | The 512-byte boot sector for a layout |
| `RunlistEncoder` | Cluster runs → data-run bytes (inverse of story-0105) |
| `AttributeBuilder` | `$STANDARD_INFORMATION`, `$FILE_NAME`, resident/non-resident `$DATA` |
| `MftRecordBuilder` | One 1024-byte record: header, attributes, update-sequence array |
| `NtfsImageBuilder` | The file set, cluster placement, and the assembled image |

The CLI grows subcommands: `revenant-imagegen pattern <output> <size> <name>`
(the story-0007 behaviour, unchanged apart from the verb) and
`revenant-imagegen ntfs <output>`. The old verb-less form is dropped — this is a
developer tool with no external consumers, and a silent dual grammar is worse
than a renamed one.

### The volume

512 B sectors, 8 sectors per cluster (4 KiB), 1024 clusters (4 MiB). `$MFT` at a
fixed cluster, 32 records of 1024 bytes; data area behind it.

| Record | Name | State | `$DATA` |
|-------:|------|-------|---------|
| 0 | `$MFT` | live | non-resident, covering the MFT itself |
| 5 | `.` (root) | live, directory | — |
| 16 | `photos` | live, directory, parent 5 | — |
| 17 | `keep.jpg` | live, parent 16 | non-resident, contiguous |
| 18 | `deleted.jpg` | **deleted**, parent 16 | non-resident, **fragmented** (two runs) |
| 19 | `notes.txt` | **deleted**, parent 5 | resident |
| 20 | `orphan.jpg` | **deleted**, parent 99 (no such record) | non-resident |

Plus one JPEG written into a cluster no record references — pure carve territory
for the hybrid pass. File payloads are structurally valid JPEGs (SOI → EOI) so
the carve engine validates them rather than merely finding a header.

## Acceptance criteria

- [x] `encodeRunlist(std::span<const DataRun>)` emits data runs with
      minimal-width fields, a signed LCN delta per run, and the `0x00`
      terminator; it round-trips through `decodeRunlist` for every run set the
      builder produces.
- [x] `makeLayout()` returns the fixed geometry, and every derived offset
      (`$MFT` byte offset, data-area start, total size) is computed once there
      rather than spelled out at use sites.
- [x] `buildBootSector(layout)` produces 512 bytes that `parseBootSector`
      accepts, returning exactly the layout's geometry.
- [x] `MftRecordBuilder` produces 1024-byte records with a correct
      update-sequence array (the last two bytes of every 512-byte stride
      replaced, USN stored in the array) that `parseMftRecord` accepts with
      `Confidence::kValid`.
- [x] Attributes are built for `$STANDARD_INFORMATION` (fixed timestamps),
      `$FILE_NAME` (UTF-16 name, parent reference, real size), and `$DATA` both
      resident and non-resident.
- [x] `writeNtfsImage(path)` writes the volume described above; identical inputs
      produce a byte-identical file (no timestamps, no randomness).
- [x] `revenant-imagegen ntfs <output>` generates it; `pattern` keeps the
      story-0007 behaviour; an unknown verb exits non-zero with a usage message.

## Test plan

Unit (`tests/unit/tools/`):

- `RunlistEncoderTest`: single run; fragmented runs; a backwards (negative
  delta) run; minimal field widths chosen; round-trip through `decodeRunlist`.
- `NtfsLayoutTest`: derived offsets and total size are self-consistent
  (`$MFT` inside the volume, data area behind the MFT, cluster-aligned).
- `AttributeBuilderTest`: each attribute's header (type, length, resident flag)
  and content parse back through the production attribute readers.
- `MftRecordBuilderTest`: a built record parses; the in-use and directory flags
  round-trip; the fixup is applied where the parser expects it.
- `NtfsImageCliTest`: `ntfs` generates a file of the expected exact size; an
  unknown verb fails; `pattern` still behaves as in story-0007.

Integration (`tests/integration/NtfsImageTest.cpp`) — the real proof, using only
production parsers over the generated file through `ImageFileDevice`:

- `parseBootSector` yields the layout's geometry.
- Every record in the table parses; states (live/deleted) and directory flags
  match; names and parent references match.
- `deleted.jpg`'s runlist decodes to two runs, maps to extents, and the bytes
  read back through those extents are the exact JPEG that was planted.
- `notes.txt`'s resident `$DATA` matches its planted content.
- Byte-identical output across two generations (determinism).

Fuzz: none — this story only *writes* self-generated bytes. The parsers it feeds
already carry their own fuzz targets.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan+UBSan).
- [x] Coverage held or raised (>= 85% core) — enforced by the CI coverage job;
      `tools/` is outside the core gate's `src`/`include` scope either way.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
