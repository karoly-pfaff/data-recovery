<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0603: The Linux device path, proven on a loop device

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: Ready
- Size: M

## Goal

Every CI run compiles `RawDevice`'s Linux half; nothing has ever executed it past
the line that turns a missing path into `kNotFound`. Attach the synthetic
partitioned disk to a real `/dev/loopN` on the WSL2 workbench and run the whole
stack against it — open, size and sector-size queries, reads through the
alignment path, `--list-partitions`, a recovery — plus the run without
privilege, which must end in the sentence M4 wrote for it rather than a bare
`EACCES`.

What "never executed" means, precisely: no test in the tree names `RawDevice`.
`SourceDeviceTest.RefusesAPathThatNamesNothing`
([`tests/unit/io/SourceDeviceTest.cpp:63`](../../../tests/unit/io/SourceDeviceTest.cpp))
reaches `RawDevice::open` only to watch a nonexistent path become `kNotFound`;
the alignment header is driven against a fake
([`tests/unit/io/AlignedReadTest.cpp`](../../../tests/unit/io/AlignedReadTest.cpp));
and the privilege sentence is asserted to be non-empty and distinct from
`kNotFound`'s, never produced by an actual refusal
([`tests/unit/cli/RunSummaryTest.cpp:114-141`](../../../tests/unit/cli/RunSummaryTest.cpp)).
The two ioctls, the open of a device that exists, and every successful read have
run zero times, on any platform.

## Design references

- [`RawDevice.hpp`](../../../include/revenant/core/io/RawDevice.hpp),
  [`RawDeviceShared.cpp`](../../../src/core/io/RawDeviceShared.cpp) — the class
  under test, and `readAt` funneling every request through
  `readThroughAlignment`.
- [`RawDevicePosix.cpp`](../../../src/core/io/RawDevicePosix.cpp) — the Linux
  measuring: `BLKGETSIZE64` for size, `BLKSSZGET` for sector size, falling back
  to 512 when the device will not say.
- [`NativeIoPosix.cpp`](../../../src/core/io/NativeIoPosix.cpp) — `open(2)` with
  `O_RDONLY | O_CLOEXEC`, `pread(2)` retried on `EINTR`, and `EACCES`/`EPERM`
  mapped to `kPermissionDenied` (everything else is `kNotFound`).
- [`src/CMakeLists.txt:159-162`](../../../src/CMakeLists.txt) — both files are
  compiled only on non-Windows platforms; CI builds them, nothing runs them.
- [`SourceDevice.cpp:49-57`](../../../src/core/io/SourceDevice.cpp) — the
  routing: not a directory, not a regular file, therefore `RawDevice::open`.
  A `/dev/loopN` node takes that third branch with no code changes.
- [`RunSummary.cpp:104-107`](../../../src/cli/RunSummary.cpp) — the sentence the
  unprivileged case must produce, quoted in the acceptance criteria below.
- [story-0401](story-0401-raw-devices.md) — the promise this story collects on,
  and the line that scoped it: "Not tested in CI: opening an actual device."
- [story-0405](story-0405-partition-selection.md) and
  [`PartitionListing.cpp`](../../../src/cli/PartitionListing.cpp) — the
  `--list-partitions` / `--partition <n>` grammar, the listing's line format,
  and the ` (read from the backup header)` note a damaged GPT earns.
- [`tools/imagegen/CliMain.cpp`](../../../tools/imagegen/CliMain.cpp) and
  [`DiskImageBuilder.hpp`](../../../tools/imagegen/disk/DiskImageBuilder.hpp) —
  `revenant-imagegen disk <output>`: an MBR disk carrying the NTFS, FAT32,
  exFAT and ext4 fixtures, each 1 MiB-aligned. The same builder
  `tests/integration/PartitionSelectionTest.cpp` runs in memory — this story is
  that test's missing other half.
- [`tools/fuzz/make_seed_corpus.py`](../../../tools/fuzz/make_seed_corpus.py)
  (`gpt_disk()`) — the checked-in `tests/fuzz/corpus/GptFuzz/gpt-disk.bin`: a
  valid two-copy GPT with computed CRCs, whose backup header sits in the last
  sector. [`GptPartitions.cpp`](../../../src/volume/GptPartitions.cpp) finds
  that sector from the device's own size, which makes `BLKGETSIZE64`
  observable.
- [`wsl-bench` skill](../../../.claude/skills/wsl-bench/SKILL.md) — the Debian
  13 workbench M5 provisioned: how to reach it, `-u root` for anything
  privileged, and the stale-loop-device trap.

## Design decisions

**A script, run by hand, recorded here — not a CI test.** The deliverable is
`tools/loopdev/verify_loop_device.sh`: plain Linux shell, driven on this
machine via `wsl.exe -d Debian -u root -- bash -lc
'/mnt/d/Projects/data-recovery/tools/loopdev/verify_loop_device.sh'`. It builds
the binaries, generates its fixtures, attaches them, runs the full pass, and
prints one verdict per check. Its transcript lands in this story on completion,
the way story-0602 records its gate output.

**The oracle is the image-file run.** `ImageFileDevice` over these exact bytes
is the best-tested code in the tree; the loop device serves the same bytes
through the one class that has never run. So every positive check is an
identity: the listing over `/dev/loopN` must be byte-for-byte the listing over
`disk.img`, and the recovered artifacts must `diff -r` clean against the
image-file run's. Any divergence belongs to `RawDevice`, because nothing else
varies.

**The kernel is the second witness.** `losetup -P` makes the kernel parse the
same MBR we do, so the partition sizes `lsblk -b` reports for `loopNp1..p4`
must equal the lengths our listing prints — two independent parsers agreeing on
one table. Reattaching with `losetup --sector-size 4096` makes `BLKSSZGET`
answer 4096, which runs story-0401's alignment arithmetic at 4Kn geometry for
the first time anywhere. And the wiped-primary GPT fixture forces the read at
`lastLbaOf(device)` — an end-of-device access whose address is computed from
`BLKGETSIZE64`'s answer, and whose success is printed as
` (read from the backup header)`.

**The pass, in order.**

1. `cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
   -DREVENANT_BUILD_TESTS=OFF`, then build `revenant-carve`,
   `revenant-undelete`, `revenant-imagegen`. No preset: every preset pins the
   vcpkg toolchain ([`CMakePresets.json`](../../../CMakePresets.json)) the
   workbench does not have, and with tests off the tree needs no dependency at
   all — `gtest` is [`vcpkg.json`](../../../vcpkg.json)'s only entry.
2. `revenant-imagegen disk "$SCRATCH/disk.img"`; copy
   `tests/fuzz/corpus/GptFuzz/gpt-disk.bin` and zero its primary header with
   `dd … bs=512 seek=1 count=1 conv=notrunc`.
3. `losetup --show -fP "$SCRATCH/disk.img"` → `/dev/loopN` plus kernel
   partitions.
4. `revenant-undelete --source /dev/loopN --list-partitions`, diffed against
   the same listing over `disk.img`; lengths diffed against `lsblk -b`.
5. `revenant-undelete --source /dev/loopN --partition 1 --destination …` —
   the NTFS fixture, per story-0405's integration test — `diff -r` against the
   image-file run.
6. Reattach with `--sector-size 4096`; whole-device `revenant-carve`, same
   identity check.
7. Attach the damaged GPT copy; the listing must say GPT and carry the
   backup-header note.
8. The same open as an unprivileged user; expect the sentence and a nonzero
   exit.

**The unprivileged case proves its own precondition.** On a stock Debian a loop
node is `root:disk 0660`, but the script does not trust the distro's group
layout: before asserting the refusal it verifies the invoking user actually
lacks read permission on the node, and fails the check as *inconclusive* — not
passed — if the user turns out to be in `disk`. A negative test that cannot
show the door was locked has shown nothing.

**Scratch and backing files live on the Linux filesystem.** The image is
written to, and attached from, the distro's own disk, not `/mnt/d` — whether
`losetup` humors a backing file on a 9p mount is a second experiment this story
does not need.

**Teardown is a trap, not a final step.** `losetup -d` runs from an `EXIT`
trap, because the wsl-bench skill already documents what a stale `/dev/loopN`
does to the next session.

## Acceptance criteria

- [ ] `tools/loopdev/verify_loop_device.sh` is committed and performs the whole
      pass above unattended on the workbench — build, fixtures, attach, checks,
      teardown — detaching its loop devices even when a check fails.
- [ ] `--list-partitions` over `/dev/loopN` prints byte-for-byte the lines it
      prints over the same bytes as an image file: the MBR heading and all four
      fixture partitions.
- [ ] The lengths our listing prints equal the sizes `lsblk -b` reports for the
      kernel's own `loopNp1..p4` scan of the same table.
- [ ] A `--partition 1` recovery out of `/dev/loopN` produces artifacts
      byte-identical to the same recovery over the image file.
- [ ] The identity holds for a whole-device carve over a `--sector-size 4096`
      attachment — the alignment arithmetic's first run at 4Kn geometry.
- [ ] The wiped-primary GPT fixture, attached, is listed as GPT with
      ` (read from the backup header)` — the end-of-device read addressed from
      `BLKGETSIZE64`'s answer.
- [ ] Run as a user the script has proven cannot read the node, the open exits
      nonzero with exactly: "the operating system refused to open the source:
      reading a whole disk or a mounted volume needs administrator (Windows) or
      root/disk-group (Linux) privilege" — and never a bare `EACCES`.
- [ ] The transcript of one full green pass is recorded in this story.
- [ ] Any defect the pass uncovers is fixed in this story behind a failing unit
      test at the platform-neutral seam that missed it, or — if it will not fit
      — split into a story numbered by the open milestone
      ([README](../README.md)).

## Test plan

The script is the test, and it is manual: run on the workbench, transcript
recorded here on completion, in the mold of story-0607's "the acceptance
criteria are the test". Every check in it is a diff or an exact-match
assertion, not an eyeball.

If a check fails, the fix follows TDD like any other change: a failing unit
test first, at whichever platform-neutral seam let the defect through
(`AlignedRead.hpp`, `PartitionTable`, `RunSummary`), so the class of bug stays
caught after this story's loop device is long detached.

Not automated (CI): the epic's premise stands — CI runners do not hand out
block devices, and story-0401 already drew the line at "opening an actual
device". Beyond that, the negative case needs a user who is genuinely refused,
which a runner with passwordless sudo can only pantomime. This scripted-manual
pass is the compensating control, and it stays manual the same way
story-0607's 32,767-character limit stays unmeasured: reviewed, recorded, and
not pretended into a gate.

## Definition of Done

- [ ] Acceptance criteria met, tests green under ASan + UBSan.
- [ ] clang-format, clang-tidy, duplication and file-length guard clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
