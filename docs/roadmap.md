<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Roadmap

Revenant is built as a sequence of **vertical, shippable milestones**. Each milestone
produces a working, tested increment — never a half-integrated horizontal layer. We
prove the full architecture on a narrow slice first, then widen coverage.

Each milestone maps to an epic in [`backlog/`](backlog/README.md), broken into stories.

| Milestone | Theme                              | Ships                                               |
|:---------:|------------------------------------|-----------------------------------------------------|
| **M0**    | Foundation & quality rig           | Build, CI, gates, `BlockDevice` + image reader, test harness |
| **M1**    | Vertical slice (proves everything) | NTFS undelete + JPEG validating carve + hybrid + CLI |
| **M2**    | Carving breadth                    | PNG, MP4/MOV, RAW, ZIP-based, PDF validators        |
| **M3**    | Filesystem breadth                 | FAT32, exFAT, ext4 undelete                          |
| **M4**    | Real devices & partitions          | Physical/volume access (Win+Linux), MBR/GPT         |
| **M5**    | 1.0 hardening                      | Performance (SIMD), packaging, docs, release        |

## M0 — Foundation & quality rig

The scaffolding this repository begins with, made executable.

- CMake + vcpkg build producing an (empty) `librevenant` and test target.
- CI green: format, tidy, file-length guard, duplication, sanitizers, coverage plumbing.
- `Result<T>`, logging, byte views, endian readers in `core/`.
- `BlockDevice` interface + `ImageFileDevice` + `InMemoryDevice`.
- Test harness and the first synthetic-image generator in `tools/`.

**Exit:** `cmake --preset debug && ctest` is green on Windows and Linux; all gates active.

## M1 — Vertical slice

The end-to-end proof: recover real files from a real (synthetic) NTFS image, by name and
by carving, in one hybrid run. This exercises every layer once.

- NTFS: boot sector → `$MFT` → deleted-entry recovery with names/paths/timestamps.
- JPEG `FormatCarver`: SOI→EOI validation with exact extent.
- Hybrid orchestration: FS pass, then carve unallocated, with dedup.
- Minimal `revenant-undelete` and `revenant-carve` CLIs.

**Exit:** given a crafted NTFS image with deleted JPEGs, both named and carved recovery
succeed, validated by golden-file tests. This is the first tagged pre-release.

## M2 — Carving breadth

Add validating carvers for the remaining priority formats, each with unit + fuzz tests
and the plausibility filter: PNG, MP4/MOV, RAW (CR2/NEF/ARW), ZIP-based
(DOCX/XLSX/PPTX), PDF. Candidate for parallel implementation (see
[performance](performance/strategy.md) and the ultracode note in `CLAUDE.md`).

## M3 — Filesystem breadth

Add FAT32, exFAT, and ext4 parsers behind the existing `FileSystem` interface, with the
same synthetic-image test methodology as NTFS.

## M4 — Real devices & partitions

Promote from images to real media: `PhysicalDevice`/`VolumeDevice` on Windows and Linux,
privilege handling, and MBR/GPT partition detection so a whole disk can be scanned.

## M5 — 1.0 hardening

Performance pass (SIMD/hand-tuned signature scanning behind benchmarks), packaging and
installers, complete user documentation, and the 1.0.0 release.

## Principles

- **YAGNI gates scope.** Nothing is built without a story; fragmentation-aware carving
  and exotic filesystems are explicitly out of scope until a milestone pulls them in.
- **Every milestone is releasable.** No milestone leaves the tree in a non-shippable
  state.
- **Quality gates never regress.** Coverage, sanitizers, and lint stay green throughout.
