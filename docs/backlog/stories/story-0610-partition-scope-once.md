<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0610: Partition scope is decided once, in `recovery/` — and the table is read once per run

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: In progress
- Size: M

## Goal

One layer decides what byte range a run works in, and it decides from one reading
of the source's partition table. Today `cli/` resolves `--partition`, builds the
window and hands it down; `recovery/` then reads a partition table *inside* that
window, finds whatever the volume's boot sector happens to look like, and walks
that instead. On a real volume — whose bootstrap area is code rather than the
zeros our fixtures carry — that is a phantom nested table, and the run it produces
is a healthy NTFS partition silently downgraded to a carve-only scan, reported as
a mounted filesystem with no files in it.

## Design references

- [epic-m5 audit](../epic-m5-performance.md#milestone-architecture-audit) lines
  138–143 — the finding, confirmed adversarially: partition orchestration split
  across two layers, and `Mbr.cpp` weak enough that VBR bootstrap bytes parse as a
  table.
- [overview.md](../../architecture/overview.md):29–32 — the layer assignment this
  restores: `cli/` is "argument parsing, config, interactive selection, progress";
  orchestration is `recovery/`.
- [hybrid-orchestration.md](../../architecture/hybrid-orchestration.md):12–13 —
  "1. Locate partitions … 2. For each partition" is step one *of the strategy*;
  and :87–95, "Configuration surface (recovery layer, not CLI) … The CLI merely
  maps flags onto this surface; the policy lives in `recovery/`."
- [story-0405](story-0405-partition-selection.md) lines 71–74 — "The selected
  partition is a `PartitionView`, and nothing below the CLI learns about
  partitions." True when it was written; superseded here, and why is below.
- [story-0407](story-0407-whole-disk-walk.md) — `enumerateDisk`, the coordinate
  translation, and the single-volume fallback this story relocates rather than
  removes.
- [story-0604](story-0604-bad-sector-map-to-manifest.md) — pins its bad-range
  translation to `RecoveryRun.cpp:163-165`, the lines this story deletes.
  Ordering is stated below.
- [story-0605](story-0605-device-loss-mid-run.md) — rewrites the same two files'
  error paths. Independent in substance; the ordering note says so.
- [`RecoveryRun.cpp:143-167`](../../../src/cli/RecoveryRun.cpp),
  [`PartitionedWalk.cpp:95-101`](../../../src/recovery/PartitionedWalk.cpp),
  [`HybridRecovery.cpp:78-91`](../../../src/recovery/HybridRecovery.cpp) — the
  three ends of the seam.

## What was measured

Verified against the current tree, every anchor re-read.

**Three sites read partition tables, across two layers.**
`volume::readPartitionTable` is called from `src/cli/RecoveryRun.cpp:144` (inside
`chosenPartition`, `:143-153`), `src/cli/PartitionListing.cpp:62`, and
`src/recovery/PartitionedWalk.cpp:96`. The listing is its own action and returns
before any recovery (`src/cli/Frontend.cpp:71-74`), so it reads once and is not
the problem. The other two are in the same run.

**`cli/` windows the device, then hands the window to an engine that re-windows
it.** `recoverFrom` (`RecoveryRun.cpp:158-167`) builds
`volume::PartitionView view{source, chosen.startBytes, chosen.lengthBytes}` at
`:164`, and that view is the device for everything after it — session shape and
checkpoints (`:99-109`), the scan, and extraction (`:127-138`). The scan reaches
`HybridRecovery::walkVolume` (`HybridRecovery.cpp:78-91`), which calls
`enumerateDisk(device, tee)` at `:82`, which reads a partition table at
`PartitionedWalk.cpp:96` — of the partition the operator already chose.

**It is three probes, not one read.** Inside the window, `readPartitionTable`
parses sector 0 as an MBR (`PartitionTable.cpp:90-93`); failing or finding
nothing, it probes LBA 1 for a GPT header (`:70-77`, `:87` →
`GptPartitions.cpp:51-52`); failing that, it reads the *last sector of the
operator's partition* looking for a backup header (`GptPartitions.cpp:45-47`,
`:21-27`).

**What `parseMbrSector` actually requires.** `0x55AA` at `0x1FE`
(`Mbr.cpp:38-46`); the four status bytes in `{0x00, 0x80}` (`:48-56`); and, for a
slot whose type byte is nonzero, a start LBA and sector count that are not zero
(`:79-90`). Nothing bounds an extent against the device — deliberately, since a
truncated image is a real recovery input. The comment at `:19-20` calls the
status check "the real test of whether a sector holds a table"; against a VBR it
is a one-in-sixteen filter per slot. Every FAT, NTFS and exFAT volume carries
`0x55AA` at `0x1FE` by definition, and bytes `0x1BE-0x1FD` are bootstrap code.

**The suite is green because our fixture bootstrap areas are zeros.**
`buildBootSector` zero-fills the sector and writes only named fields and the
signature (`tools/imagegen/ntfs/BootSectorBuilder.cpp:51`, `:67`), so all four
phantom slots read as *unused*, `parseMbrSector` **succeeds**, `partitionsOf`
contributes nothing (`MbrPartitions.cpp:53-62`), and the empty-table branch at
`PartitionedWalk.cpp:97-98` falls back to `enumerateVolume`. The fallback covers
an unparseable or empty table. It does not cover a table that parses and lists
things.

**Where a phantom table actually leads — quieter and worse than "bogus
sub-windows".** Each phantom entry becomes a `PartitionView` on the view
(`PartitionedWalk.cpp:70`) whose start is almost certainly past the window's end,
so `clampLength` returns 0 without an error (`PartitionView.cpp:22-28`); a
zero-length device will not mount; `walkOne` swallows the failure and returns the
running total (`:73-75`); `walkPartitions` returns zeros and `enumerateDisk`
returns a **value** (`:95-101`). So `walkVolume` records
`.mounted = true, .entries = 0, .nonConforming = false`
(`HybridRecovery.cpp:86-90`), the empty accounting yields one gap covering the
device (`:116`), and the run carves the whole partition and exits 0. An undelete
of an intact volume becomes a carve-only scan, and both flags that exist to say
something was wrong with the volume — `filesystemMounted`, `nonConformingVolume`
— say everything was fine.

**The layer assignment says the opposite of the code.**
`overview.md:29-32` gives `cli/` argument parsing and selection and `recovery/`
orchestration; `hybrid-orchestration.md:12-13` makes "locate partitions, then for
each partition" step one of the strategy, and `:95` says in as many words that the
policy lives in `recovery/`. `RecoveryRun.hpp:25-27` promises "a frontend carries
the operator's choice of policy, never a policy of its own", one file away from
the frontend resolving a partition table (`RecoveryRun.cpp:143-153`).

**How it got here.** story-0405 put the window in `cli/` when `recovery/` had
never heard of partitions, and said so on purpose (lines 71–74). story-0407 then
taught `recovery/` to walk a whole disk as all of its volumes, and the two halves
have been reading tables past each other since. Neither story was wrong when it
landed; nothing reconciled them, because the layer DAG is enforced by no gate.

## Design decisions

**`cli/` passes a number. `recovery/` decides everything else.** The operator's
choice is already just a number by the time the grammar is done with it
(`RecoveryOptions.cpp:189` → `RunRequest::partition`, `RecoveryRun.hpp:36-39`,
where zero already means the source itself). `cli/` stops resolving it: both
`chosenPartition` and the `PartitionView` construction leave `RecoveryRun.cpp`,
and with them every `volume::` name and include in that file.

**A `RunScope`, resolved once, in `recovery/`.** New unit,
`include/revenant/recovery/RunScope.hpp` — `recovery/` may depend on `volume/`;
that is the direction the DAG runs. `RunScope::resolve(BlockDevice& source,
std::uint32_t partition)` performs the run's one and only `readPartitionTable`
and answers three things: the device the run works in (the source, or the window),
where the run's zero sits on the source (`startBytes()`), and how the filesystem
pass must read it (`layout()`, plus the partitions when there are any). It
borrows the source, which `runRecovery` keeps alive for the whole run, and owns
the window; it is constructed once and never reassigned, which is all a view
holding a parent reference permits.

The window is a `std::unique_ptr<volume::PartitionView>` rather than the
`std::optional` this story first specified, and the reason is not style.
`BlockDevice` deletes both copy and move (`BlockDevice.hpp:19-22`), so a
`PartitionView` is neither, so an `optional` holding one is neither — and
`Result<T>`'s constructor takes `T` by value, so a scope owning the window by
value could not be returned from `resolve` at all. Measured, not reasoned:
`static_assert(!std::is_move_constructible_v<std::optional<PartitionView>>)`
holds. Indirection buys a second thing worth having — the view stays put when
the scope moves, so the pointer the scope hands out as `device()` stays valid
across the return.

**`HybridRecovery::run` takes the scope, not a bare device.** The device and the
layout arrive together and therefore cannot disagree — which is precisely the bug:
today the device says "I am one partition" and the layout is inferred, badly, from
its bytes. `RecoveryPlan` is untouched: it carries policy (mode, resume,
checkpoint interval); the scope carries *where*, and those are different questions.

**`enumerateDisk` is handed its partitions and stops reading tables.** New
signature: `enumerateDisk(BlockDevice&, std::span<const volume::Partition>,
fs::EntryVisitor&)` — it walks exactly what it is given, in the coordinates of the
device it is given, with story-0407's translation and prefixing unchanged. The
single-volume fallback at `:97-98` moves up into the resolver, which is the only
place holding the table that the fallback is a decision *about*. The alternative
the audit floated — a disk-vs-volume flag on `enumerateDisk` — was rejected:
it would leave the table read inside `enumerateDisk` and so leave two readers, and
its two branches are `enumerateDisk` and `enumerateVolume`, two functions that
already exist. A flag selecting which of two functions to call, written inside one
of them, is not a scope.

**A table entry names a volume; a volume does not contain a partition table.** So
a scoped run never looks for one — that is the whole fix, and it costs nothing,
because the one genuinely nested structure on real disks is the MBR extended
chain, which `readMbrPartitions` already flattens into the same numbered list
(`MbrPartitions.cpp:39-47`). No number an operator can type names a container.

**`Mbr.cpp`'s validation is not tightened, on purpose.** The obvious patch —
reject a slot whose extent runs past the device — would reject the real table on a
truncated image, and imaging what you can reach is a thing this tool exists to
support (`PartitionView` clamps such a window today, which is the right answer).
Anything strong enough to reject genuine bootstrap code is a heuristic, and a
heuristic in the volume layer is the "keep reading until it looks right" habit
[ADR-0003](../../architecture/adr/adr-0003-validating-carving.md) was written
against. The parser is not wrong; asking a volume whether it is a disk is. This
story stops asking.

**The listing keeps its own read.** `describePartitions` opens a source, reads the
table once and prints it (`PartitionListing.cpp:61-77`). Reading is `volume/`'s
job and formatting is `cli/`'s, the call is downward, and a listing run is already
one read. Moving it would buy a layer diagram and cost a straight line.

**Behaviour is preserved to the byte, deliberately.** A scoped run still writes
unprefixed paths; a whole-disk run still writes `partition-N/`; a number the table
does not carry is still `kNotFound` rather than a silent whole-source run; a
source with no readable table is still walked whole. The only run whose output
changes is the one with a phantom table, and it changes from wrong to right.

**Ordering: before story-0604; independent of story-0605.** 0604 names
`RecoveryRun.cpp:163-165` as the holder of `startBytes` for translating
view-relative extents into the device-absolute offsets its manifest promises
"including in a partition run" — those are the lines this story deletes. Landing
0610 first means 0604 writes that translation once, against `RunScope::startBytes()`,
instead of against a lambda that is about to move. Nothing runs the other way:
this story needs no decorators, no bad-sector map, and is indifferent to
`openSource` returning a bare device or an owning stack, because its seam takes a
`BlockDevice&` either way. 0605 is independent in substance — it changes error
taxonomy and exit codes, not scope — and already sits after 0604, so the sequence
is 0610 → 0604 → 0605, and the only thing that would be gained by reordering is
merge conflicts in `RecoveryRun.cpp`.

## Acceptance criteria

- [ ] `volume::readPartitionTable` has exactly two callers in `src/`: the scope
      resolver in `recovery/`, and `describePartitions` in `cli/`. Neither is
      reachable twice in one run.
- [ ] `src/cli/RecoveryRun.cpp` includes no `volume/` header and names no
      `volume::` type; the partition number is all it passes down.
- [ ] `enumerateDisk` takes the partitions it is to walk, and contains no call to
      `readPartitionTable` and no single-volume fallback.
- [ ] A run scoped to a partition whose volume's bootstrap area parses as a valid
      MBR recovers exactly what the same run over the unmodified fixture recovers:
      same files, same paths, same bytes, same `RecoveryStats`.
- [ ] A whole-source run over the partitioned disk still reports every mountable
      partition under `partition-N/`; over a single-volume image, still the paths
      it has always had.
- [ ] `--partition <n>` naming a number the table does not carry is still
      `kNotFound`; a source whose table cannot be read at all still refuses a
      scoped run instead of running whole.
- [ ] The scope exposes where the run's zero sits on the source, so story-0604's
      translation has one named owner.
- [ ] [hybrid-orchestration.md](../../architecture/hybrid-orchestration.md) and
      [overview.md](../../architecture/overview.md) describe where partition scope
      is decided as it now is; `CHANGELOG.md` updated under `[Unreleased]`.

## Test plan

Unit (`tests/unit/recovery/RunScopeTest.cpp`, new — the decision now has a home,
so it gets a test file): the partitioned disk with choice 0 resolves to the disk's
partitions with zero offset; with choice 2, to a window at that entry's offset and
length, walked as a single volume; with choice 9, to `kNotFound`; a single-volume
image with choice 0 resolves to one volume (story-0407's fallback, relocated); a
source whose table will not read refuses choice 3; and the audit's case — the
phantom fixture with choice 1 resolves to a single volume, because nothing looks
inside a window.

Unit (`tests/unit/recovery/PartitionedWalkTest.cpp`): the six existing cases
re-pointed at the narrowed signature, asserting what they assert now — disk
coordinates, partition-qualified paths, summed stats, one dead volume not stopping
the others. The one that exercises the fallback moves to `RunScopeTest`, following
the decision. (The story said *two*; there is one — only
`AnUnpartitionedVolumeWalksWithTheNamesItAlwaysHad` builds an unpartitioned
image. That the single-volume path still writes unprefixed paths is asserted by
`HybridRecovery.TheNamedFilesKeepTheirPaths`, which now reaches it through a
resolved scope.)

Fixture (`tools/imagegen/disk/DiskImageBuilder`): a sibling of
`buildMbrDiskImage()` that builds the same disk and then writes a valid-looking
four-slot table into partition 1's first sector — one used slot, status `0x00`,
type `0x07`, a nonzero start LBA *inside* the volume and a nonzero sector count,
so the phantom window is well formed rather than clamped to nothing and the test
is about scope rather than about damage. It belongs in `tools/imagegen` for two
reasons: that is where every fixture's bytes are decided, and `writeEntry` /
`writeTable` (`DiskImageBuilder.cpp:71-86`) already write exactly these 66 bytes
one megabyte earlier — the new builder is the same writer at a different base.
`0x1BE-0x1FD` holds none of NTFS's BPB fields, so the volume still mounts
perfectly, which is what makes the fixture adversarial. The in-test `fill_n`
damage at `PartitionedWalkTest.cpp:102-104` is the precedent for a one-line
mutation; a crafted table is not one.

Integration (`tests/integration/PartitionSelectionTest.cpp`): a fourth case in the
existing shape — `--partition 1` over the phantom disk recovers
`photos/deleted.jpg` byte-identically, exactly as case one does over the clean
disk. It fails on today's code, where the filesystem pass reports nothing and the
file arrives, if at all, under `carved/` with a generated name.

Regression: `tests/integration/WholeDiskRecoveryTest.cpp` and the three existing
`PartitionSelection` cases pass unchanged. The three integration suites that
construct `HybridRecovery{…}.run(device, …)`
(`HybridRecoveryTest.cpp`, `RecoveredFilesTest.cpp`, `ArbitratedRecoveryTest.cpp`)
and `tests/unit/recovery/HybridRecoveryTest.cpp` are re-pointed through
`RunScope::resolve(device, 0)`; behaviour is identical, because `enumerateDisk`
was already reading that table on their behalf.

No new fuzz target: this story adds no byte parser and deliberately changes none.
`tests/fuzz/MbrFuzz.cpp` and its corpus stand.

## Definition of Done

- [ ] Acceptance criteria met, tests green under ASan + UBSan.
- [ ] clang-format, clang-tidy, duplication and file-length guard clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
