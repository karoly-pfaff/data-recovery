<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M4 — Real devices & partitions

**Goal:** promote from disk images to real media. Add physical/volume `BlockDevice`
implementations for Windows and Linux, privilege handling, and MBR/GPT partition
detection so a whole disk can be scanned end to end.

**Milestone:** [M4](../roadmap.md#m4--real-devices--partitions)

## Outcome / definition of ready-to-close

- `PhysicalDevice` and `VolumeDevice` read real disks read-only on Windows and Linux.
- The `volume/` layer parses MBR and GPT and yields the partition list.
- `revenant-undelete`/`revenant-carve` can target a whole physical disk and iterate its
  partitions.
- Clear, actionable errors when privileges are insufficient.

## Candidate stories (expanded when picked up)

| Story | Title | Size |
|-------|-------|:----:|
| story-0401 → | see [story-0401](stories/story-0401-raw-devices.md): `RawDevice` — whole disks and volumes, Windows *and* Linux | M |
| story-0402 → | see [story-0402](stories/story-0402-io-decorators.md): `CachingDevice` + `RetryingDevice`, against a fault-injecting device | M |
| story-0403 → | see [story-0403](stories/story-0403-mbr-partition-table.md): MBR partition table parser and EBR chain | S |
| story-0404 → | see [story-0404](stories/story-0404-gpt-partition-table.md): GPT partition table parser, backup header fallback, protective MBR | M |
| story-0405 → | see [story-0405](stories/story-0405-partition-selection.md): one table whichever scheme wrote it, `--list-partitions`, `--partition <n>` | M |
| story-0406 → | see [story-0406](stories/story-0406-reject-file-shares.md): refuse a file-level source, and say what to point at instead | S |
| story-0407 → | see [story-0407](stories/story-0407-whole-disk-walk.md): a whole disk walked as all of its partitions | M |

Two stories M4 sketched are **not** in that table. A `NetworkBlockDevice` and an
imaging mode were both planned here and both deferred; they are
[M8](epic-m8-acquisition-damaged-media.md)'s, and take their numbers there.

Two others changed shape while being written. What was to be a second story beside
story-0401 was folded into it: the two named classes turned out to be one, and splitting
its platform halves across two stories would have left `main` linking against a stub on
one of them. story-0407 was split out of story-0405 for the opposite reason: selecting
one partition and iterating all of them are separately deliverable, and the second
reaches into the recovery layer while the first does not leave the CLI. Both are needed
for the outcome above.

## Notes

- Platform code stays confined to `core/io/`, selected by CMake, not scattered `#ifdef`.
- Bad-sector tolerance ([io-layer.md](../architecture/io-layer.md)) is validated against
  a fault-injecting device in tests before touching real failing hardware.

## Milestone architecture audit

Run at the milestone boundary, per [code-quality.md](../code-quality.md).

- **Did a layer leak?** No. `recovery/` gained a dependency on `volume/` (the whole-disk
  walk reads the table and opens a `PartitionView`), and `cli/` did the same for
  `--partition`. Both are *downward* in the layer diagram, which is the direction
  dependencies are allowed to run.
- **Did the interfaces hold?** `BlockDevice` did, and did more work than before: a
  partition, a cache, a retry layer and a raw device are all one, and the filesystem
  parsers above learned nothing about any of them. Two documented names did **not**
  survive contact — `PhysicalDevice` and `VolumeDevice` turned out to be one class — and
  [io-layer.md](../architecture/io-layer.md) now says so. No new ADR was needed: nothing
  about the *boundary* changed, only the count of classes behind it.
- **Did complexity creep in?** Once, and the duplication gate caught it: `RawDevice`
  began as `ImageFileDevice` copied. Both now derive from `NativeSourceDevice` and share
  four platform primitives. That refactor landed inside story-0401 rather than being
  deferred, which is the rule this audit exists to enforce.
- **Anything to automate?** Nothing new. The two failures that cost the most round trips
  this milestone — a partial designated-initializer clang rejects but MSVC accepts, and a
  `std::array` iterator held in `auto` — are both already caught, by the local clang build
  and by CI's clang-tidy respectively.
- **Findings become stories:** [story-0601](epic-m6-loose-ends.md) — `SafeArith`
  now has a caller outside `fs/` and should not keep that namespace.
