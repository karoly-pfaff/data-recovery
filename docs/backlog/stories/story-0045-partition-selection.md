<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0045: Partition enumeration and selection

- Epic: [epic-m4-devices-partitions](../epic-m4-devices-partitions.md)
- Status: Done
- Size: M

## Goal

Turn two parsers into something an operator can use: one scheme-agnostic reading
of a disk's layout, a `--list-partitions` that prints it, and a `--partition N`
that runs recovery inside one of them. Until now every source was assumed to be a
single volume; a real disk is not.

## Design references

- [story-0043](story-0043-mbr-partition-table.md) and
  [story-0044](story-0044-gpt-partition-table.md) — the two schemes, and
  `volume::defersToGpt`, the question that chooses between them.
- [`volume::PartitionView`](../../../include/revenant/volume/PartitionView.hpp) —
  the byte window a selected partition becomes.
- [ADR-0005](../../architecture/adr/adr-0005-read-only-by-default.md) — listing
  reads sector 0 and nothing else; it writes nothing and needs no destination.

## Scope

1. **One table, either scheme** (`include/revenant/volume/PartitionTable.hpp`) —
   `readPartitionTable` reads sector 0, asks it which scheme owns the disk, and
   returns `PartitionTable{scheme, partitions, fromBackupHeader}` with each
   `Partition{startBytes, lengthBytes, number, label}`.
2. **A label a person can read** — the well-known MBR type bytes and GPT type
   GUIDs get names; a GPT entry's own name wins over both when it has one.
3. **`--list-partitions`** on both frontends — prints the table over `--source`
   and stops. No destination is required, because nothing is written.
4. **`--partition <n>`** on both frontends — the run happens inside that
   partition's byte window instead of over the whole source.

## Design decisions

**The scheme question is asked of sector 0, and only sector 0.** `defersToGpt`
already answers it for a table that parses. A sector that does *not* parse as a
table is not the end of the enquiry, though: a wiped or overwritten sector 0 is
one of the commonest things a recovery tool meets, and the GPT that survives it
is two sectors away. So the fall-through is to try the GPT anyway, and only a
disk that has neither is refused — with sector 0's own rejection, which is the
one that describes what an operator would look at first.

**A partition's number is the table's order, one-based, and it is stored rather
than derived.** It is what the operator types back, so it has to survive the
listing being filtered, sorted or printed twice. Making the caller count is how
a `--partition 3` comes to mean a different partition than the `3` it was read
from.

**A label is a convenience, and it is allowed to be one.** It is the one line
that lets someone recognize their own disk, so the well-known type bytes and
type GUIDs are named — but the list is deliberately short, and anything else
falls back to the raw type rather than growing a registry of every vendor code
that has ever existed. Offset and size disambiguate whatever the label does not.

**Listing is not a recovery, so it does not ask for a destination.** Requiring
one would mean an operator has to name a place to write before they can find out
what is on the disk, which inverts the order in which the questions actually get
asked. It also keeps the read-only guarantee visible: this path opens the source,
reads sector 0 and its table, and returns.

**`--partition 0` does not exist.** Partitions are numbered from one, so zero is
free to mean "the source itself" internally, and an operator who types `0` gets
the usage text rather than a silent whole-disk run.

**The selected partition is a `PartitionView`, and nothing below the CLI learns
about partitions.** The recovery engine takes a `BlockDevice`; a partition is
one. Every offset the run reports is therefore partition-relative, which is what
the filesystem inside it means by them.

## Acceptance criteria

- [x] `readPartitionTable` returns `PartitionScheme::kMbr` and the MBR's
      partitions for a disk with a normal table.
- [x] A disk whose sector 0 carries a `0xEE` entry — protective or hybrid —
      returns `PartitionScheme::kGpt` and the GPT's partitions.
- [x] A disk whose sector 0 is unreadable as a table still returns the GPT's
      partitions when one is there.
- [x] A source with neither is sector 0's own rejection.
- [x] `fromBackupHeader` is carried through from the GPT read.
- [x] Partitions are numbered from 1 in table order.
- [x] An MBR partition's label names the well-known type bytes and falls back to
      `type 0xNN`; a GPT partition's label is its own name, falling back to the
      well-known type GUIDs and then to `GPT partition`.
- [x] `--list-partitions` prints one line per partition — number, size, offset,
      label — plus a heading naming the scheme, and needs no `--destination`.
- [x] A source with no partition table is listed as a single volume rather than
      reported as a failure.
- [x] `--partition <n>` runs the recovery inside partition `n`; a number that is
      not in the table is `kNotFound`, and a non-numeric or zero argument is a
      usage error.
- [x] `--partition` and `--list-partitions` are each refused when stated twice.

## Test plan

Unit (`tests/unit/volume/PartitionTableTest.cpp`): an MBR disk; a protective-MBR
GPT disk; a hybrid disk; a GPT disk with sector 0 wiped; a disk with neither; the
numbering; `fromBackupHeader` carried through.

Unit (`tests/unit/volume/PartitionLabelTest.cpp`): a known MBR type byte, an
unknown one, a named GPT entry, an unnamed one with a known type GUID, an unnamed
one with an unknown type GUID.

Unit (`tests/unit/cli/PartitionListingTest.cpp`): the lines a table produces, and
the line an unpartitioned source produces.

Unit (`tests/unit/cli/UndeleteOptionsTest.cpp`,
`tests/unit/cli/CarveOptionsTest.cpp`): `--list-partitions` without a
destination; `--partition 2`; `--partition 0`; `--partition x`; each flag twice.

Integration (`tests/integration/PartitionSelectionTest.cpp`): the synthetic disk
`imagegen::disk::buildMbrDiskImage` builds — all four filesystem fixtures as
partitions, each aligned as a real partitioner aligns them. `--partition 1`
recovers the NTFS volume's files; `--partition 2` does not reach them;
`--partition 9` is refused.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
