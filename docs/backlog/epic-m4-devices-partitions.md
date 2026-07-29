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
| story-0040 → | see [story-0040](stories/story-0040-raw-devices.md): `RawDevice` — whole disks and volumes, Windows *and* Linux | M |
| story-0041 | folded into story-0040 | — |
| story-0042 → | see [story-0042](stories/story-0042-io-decorators.md): `CachingDevice` + `RetryingDevice`, against a fault-injecting device | M |
| story-0043 → | see [story-0043](stories/story-0043-mbr-partition-table.md): MBR partition table parser and EBR chain | S |
| story-0044 → | see [story-0044](stories/story-0044-gpt-partition-table.md): GPT partition table parser, backup header fallback, protective MBR | M |
| story-0045 → | see [story-0045](stories/story-0045-partition-selection.md): one table whichever scheme wrote it, `--list-partitions`, `--partition <n>` | M |
| story-0046 | `NetworkBlockDevice` (remote raw device: iSCSI/NBD) — ADR-0007 | L |
| story-0047 → | see [story-0047](stories/story-0047-reject-file-shares.md): refuse a file-level source, and say what to point at instead | S |
| story-0048 | Imaging mode: forward-only, bad-sector-tolerant acquisition + bad-sector map | L |
| story-0049 | Whole-disk runs: the filesystem pass walks every partition | M |

story-0041 was folded into story-0040 while it was being written: the two named
classes turned out to be one, and splitting its platform halves across two stories
would have left `main` linking against a stub on one of them. story-0049 was split
out of story-0045 while it was being written: selecting one
partition and iterating all of them are separately deliverable, and the second
reaches into the recovery layer while the first does not leave the CLI. Both are
needed for the outcome above.

## Notes

- Platform code stays confined to `core/io/`, selected by CMake, not scattered `#ifdef`.
- Bad-sector tolerance ([io-layer.md](../architecture/io-layer.md)) is validated against
  a fault-injecting device in tests before touching real failing hardware.
