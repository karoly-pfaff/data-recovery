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
| **M5**    | Performance                        | One-pass matcher, AVX2 fast path, range sharding    |
| **M6**    | Loose ends & untested paths        | Audit debts, gate parity, the Linux device path, failure paths |
| **M7**    | 1.0 release                        | Packaging, documentation, the `1.0.0` tag           |
| **M8**    | Acquisition & damaged media        | Imaging mode + bad-sector map, remote raw devices   |

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
- Candidate index + confidence arbitration; hybrid FS-then-carve with dedup.
- **Output safety** ([ADR-0009](architecture/adr/adr-0009-output-safety.md)): path
  confinement + bounded allocation — foundational, so it lands here, not later.
- **Filename decoding** (NTFS UTF-16 → safe output,
  [ADR-0010](architecture/adr/adr-0010-filename-decoding-safe-output.md)).
- **Session manifest** (provenance + SHA-256 + bad-sector map) and **`--dry-run`**
  preview ([recovery-output](architecture/recovery-output.md)).
- **Resumable scan** via the durable candidate index
  ([ADR-0008](architecture/adr/adr-0008-resumability-checkpointing.md)).
- Minimal `revenant-undelete` and `revenant-carve` CLIs.

**Exit:** given a crafted NTFS image with deleted JPEGs, both named and carved recovery
succeed with a manifest, survive an interrupted-and-resumed run, and cannot write outside
the destination — all validated by tests. This is the first tagged pre-release.

Safety, manifest, and resumability are foundational cross-cutting concerns: proving them
on the narrow M1 slice is cheaper and safer than retrofitting them across a wide surface.

## M2 — Carving breadth

Add validating carvers for the remaining priority formats, each with unit + fuzz tests
and the plausibility filter: PNG, MP4/MOV, RAW (CR2/NEF/ARW), ZIP-based
(DOCX/XLSX/PPTX), PDF. Candidate for parallel implementation (see
[performance](performance/strategy.md)).

## M3 — Filesystem breadth

Add FAT32, exFAT, and ext4 parsers behind a shared `fs::FileSystem` seam — which M3
builds first, since M1 deliberately shipped without one — with the same synthetic-image
test methodology as NTFS.

## M4 — Real devices & partitions

Promote from images to real media: `PhysicalDevice`/`VolumeDevice` on Windows and Linux,
privilege handling, and MBR/GPT partition detection so a whole disk can be scanned.

## M5 — Performance

The signature scanner touches every byte of every disk we read, and it is the only thing
this milestone is about. First the measurement — process-level, so it can see memory and
I/O, and gated on metrics that survive being taken on two different machines. Then the
algorithm: the portable matcher currently runs one search *per signature* over every
window, and fixing that is a larger win than any instruction set. Then, and only then,
the AVX2 fast path, behind a runtime check and a differential test. Range sharding closes
it, and may close it with a measured "no".

## M6 — Loose ends & untested paths

Everything the earlier milestones borrowed and did not pay back: the M4 audit's open
finding, the one gate that still needs Node.js, the Linux device path that has only ever
been compiled, the failure modes nobody has provoked, and the fuzzing and soak runs that
never fit in a CI budget. No new capability — this is the milestone that makes the
existing capability trustworthy.

## M7 — 1.0 release

Packaging and installers for Windows and Linux, complete user documentation including the
recovery playbook and an honest page of limitations, and the `1.0.0` tag. From that tag
the compatibility promise in [versioning.md](versioning.md) binds.

## M8 — Acquisition & damaged media

The first milestone after 1.0, and it is defined by 1.0's own advice. The recovery
playbook M7 writes tells the operator to image a failing drive before touching it, and
then sends them to `ddrescue` — because Revenant cannot. M8 makes it able to: a
forward-only, bad-sector-tolerant, resumable acquisition that emits an image *and* the
map of what it could not read; an I/O layer where a hole in that image is **unknown**
rather than zeros, so no carver ever validates invented bytes; and `NetworkBlockDevice`
for remote raw devices, which [ADR-0007](architecture/adr/adr-0007-block-level-access-boundary.md)
has always had a place for.

Fragmentation-aware carving and exotic filesystems are deliberately *not* here — M8 is
about acquiring bytes, not about interpreting them — and stay unscheduled.

## Principles

- **YAGNI gates scope.** Nothing is built without a story; fragmentation-aware carving
  and exotic filesystems are explicitly out of scope until a milestone pulls them in.
- **Every milestone is releasable.** No milestone leaves the tree in a non-shippable
  state.
- **Quality gates never regress.** Coverage, sanitizers, and lint stay green throughout.
