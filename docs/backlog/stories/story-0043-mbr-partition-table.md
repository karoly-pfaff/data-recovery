<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0043: MBR partition table parser

- Epic: [epic-m4-devices-partitions](../epic-m4-devices-partitions.md)
- Status: Done
- Size: S

## Goal

Read the oldest thing on a disk: the four-entry table in sector 0 that says
where each partition begins and how long it is, and the EBR chain an extended
partition hangs off it. The product is a list of byte ranges — exactly what
[`volume::PartitionView`](../../../include/revenant/volume/PartitionView.hpp)
was built to open and `fs::mountVolume` was built to be handed.

## Design references

- [I/O layer](../../architecture/io-layer.md) — every source is a
  `BlockDevice`; a partition is a window over one.
- [ADR-0009](../../architecture/adr/adr-0009-output-safety.md) — bounded
  allocation. The EBR chain is a linked list read from untrusted bytes, so it
  needs a length bound *and* a revisit check, exactly as ext4's orphan list has.
- [story-0032](story-0032-exfat-boot-and-entry-sets.md) — the shape this
  follows: one validator per field, each reporting its own byte offset.

## Scope

1. **The sector parser** (`include/revenant/volume/Mbr.hpp`) —
   `parseMbrSector` validates a 512-byte sector-0 image and returns the four
   16-byte entries as `MbrTable`: type byte, starting LBA, sector count.
2. **The device read** (`include/revenant/volume/MbrPartitions.hpp`) —
   `readMbrPartitions` reads sector 0, parses it, and turns every entry into an
   `MbrPartition`: absolute byte offset, byte length, type byte, and whether it
   came from the primary table or the extended chain.
3. **The extended chain** (`src/volume/ExtendedChain.hpp`) — an entry of type
   `0x05`, `0x0F` or `0x85` is a container, not a partition. Its EBR chain is
   walked, bounded and cycle-checked, and each link contributes one logical
   partition.
4. **A fuzz target** (`MbrFuzz`) driving the whole read — parser *and* chain —
   over an in-memory device, with a seeded corpus.

## Design decisions

**The boot flag is a discriminator, not data.** Nothing in recovery cares which
partition BIOS would have booted, so the flag is not reported. It is still
*read*, because it is the one field that tells an MBR from the boot sector of an
unpartitioned volume: both carry `0x55AA` at `0x1FE`, but only in a real table
are all four status bytes `0x00` or `0x80`. Four boot code bytes landing in that
set by accident is a one-in-four-billion event; the signature alone is a
one-in-65536 one.

**The CHS fields are not read at all.** They cannot address a modern disk and
every writer since the 1990s has filled them with the `0xFE 0xFF 0xFF` "out of
range" placeholder. A parser that consulted them would be believing a field
whose own writers gave up on it. The LBA pair at `0x08`/`0x0C` is the truth.

**A used entry starting at LBA 0 is a broken table.** LBA 0 is the table's own
sector; nothing can be partitioned there. Rejecting it costs nothing and it is a
second, independent reason a boot sector's code bytes will not pass as a table.
An *unused* entry (type `0x00`) is not checked at all — writers leave stale
bytes behind a zeroed type byte, and reading them would invent partitions.

**An EBR is an MBR-shaped sector, so it is parsed by the same function.** Same
signature, same four slots, same status bytes. What differs is only how the two
used slots are *addressed*, and that is the chain walk's knowledge, not the
sector parser's.

**The two relative addresses in an EBR are relative to different things.** This
is the classic way to get an extended partition wrong. Slot 0 describes the
logical partition and its start is relative to **the EBR that holds it**. Slot 1
points at the next EBR and its start is relative to **the head of the extended
partition**, the same base for every link in the chain. Using one base for both
yields a chain that appears to work on a disk with a single logical partition
and silently misplaces every partition after that.

**The chain is bounded twice, because one bound is not enough.** A revisit check
alone lets a crafted table build a 4-billion-link acyclic chain; a length cap
alone lets a two-link cycle spin until it hits the cap. Both are cheap: the walk
stops at `kMaxLogicalPartitions` links and at the first LBA it has already
visited. A chain that cannot be followed — an unreadable sector, a sector that
is not a valid EBR, an LBA past the end of the device — *ends*, returning what
was found so far. This matches `fs::ext4::orphanInodes`: a corrupt tail of a
chain must not cost the caller the head of it.

**A read is bounded to the device; a report is not.** Every sector this walk
reads is checked against the device's size first, and that check is a division,
so the byte offset it then computes provably cannot overflow. What the *table*
says about a partition is passed through as stated. A disk image truncated for
testing, or a partition whose end was lost with the tail of a failing drive,
still enumerates — `PartitionView` clamps what it opens, which is the right
place for it, and a partition the user can see is one they can reason about.

**A protective MBR is a keep-out sign, not a partition table.** A type-`0xEE`
entry means every byte of the disk belongs to a GPT and a legacy tool must not
touch it. Returning it as a partition would hand the caller one bogus
whole-disk range instead of the real ones, so `readMbrPartitions` rejects the
table at that entry's type byte. Reading what is actually there is
[story-0044](story-0044-gpt-partition-table.md)'s job.

## Acceptance criteria

- [x] `parseMbrSector` returns `MbrTable{entries}` — four `MbrEntry{type,
      startLba, sectorCount}` read from `0x1BE` at 16 bytes apart.
- [x] Input shorter than 512 bytes is `kOutOfRange` at the input's size.
- [x] A signature other than `0x55 0xAA` at `0x1FE` is `kInvalidArgument` at
      `0x1FE`.
- [x] A status byte that is neither `0x00` nor `0x80` is `kInvalidArgument` at
      that entry's own offset.
- [x] A used entry with a zero sector count is `kInvalidArgument` at its `0x0C`;
      one starting at LBA 0 is `kInvalidArgument` at its `0x08`. An unused entry
      (type `0x00`) is accepted whatever its other bytes hold.
- [x] `readMbrPartitions` yields one `MbrPartition{startBytes, lengthBytes,
      typeCode, logical}` per used primary entry, in table order.
- [x] An unreadable or invalid sector 0 is the read's typed error; a device
      whose sector size is 0 is `kInvalidArgument`.
- [x] A table containing a `0xEE` entry is `kInvalidArgument` at that entry's
      type byte.
- [x] An entry of type `0x05`, `0x0F` or `0x85` contributes its chain's logical
      partitions instead of itself, each with `logical == true`, each placed at
      its own EBR's offset plus its stated start.
- [x] The next EBR is found at the *extended partition's* start plus slot 1's
      stated start, not at the current EBR's.
- [x] A chain that revisits an LBA stops there; a chain longer than
      `kMaxLogicalPartitions` stops at the cap; an unreadable, invalid or
      off-device link ends the chain and keeps what came before it.
- [x] `MbrFuzz` exists with a seeded corpus and drives `readMbrPartitions` over
      an in-memory device.

## Test plan

Unit (`tests/unit/volume/MbrTest.cpp`): a known-good table parses to known
entries; one case per rejection above, asserting code *and* offset through
`testing::Rejection`; an unused entry holding stale non-zero bytes is accepted;
a table of four unused entries parses to four unused entries.

Unit (`tests/unit/volume/MbrPartitionsTest.cpp`): a two-partition disk yields
two partitions with byte offsets scaled by the device's sector size; a 4096-byte
sector device scales by 4096; a disk with an extended partition holding two
logical partitions yields both, at the offsets their own EBRs place them; a
chain whose second EBR pointer is relative to the wrong base lands somewhere the
test can tell apart; a self-referencing chain terminates; a chain of 200 links
stops at the cap; a chain whose next link is past the end of the device keeps
what it found; a protective MBR is rejected at `0x1C2`; a zero-sector-size
device is rejected.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
