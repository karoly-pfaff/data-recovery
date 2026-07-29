<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0047: Refuse a file-level source, and say why

- Epic: [epic-m4-devices-partitions](../epic-m4-devices-partitions.md)
- Status: Done
- Size: S

## Goal

Someone will point `--source` at `\\server\share`, or at a folder full of files,
and expect recovery. It cannot work — a folder exposes only *live* files, and
there is nothing there to undelete or carve. Say so, in a sentence that explains
what to point at instead, rather than failing with whatever the OS says when
asked to open a directory as a disk.

## Design references

- [ADR-0007](../../architecture/adr/adr-0007-block-level-access-boundary.md) —
  the boundary this enforces: "Revenant detects a file-level path used where a
  block source is required and rejects it with a clear message explaining that
  block-level access is needed."
- [story-0040](story-0040-raw-devices.md) — `openSource`, the single place a
  source path becomes a device, and therefore the single place to refuse one.

## Scope

1. **`ErrorCode::kNotBlockAddressable`** — the refusal, told apart from the four
   failures that already exist because the action it calls for is different.
2. **The check** — `openSource` refuses a directory before it tries to open it.
3. **The sentence** — what `describe` prints, naming both what is wrong and what
   would work.

## Design decisions

**A directory is the whole rule.** A share root (`\\server\share`), a mounted
NFS or SMB path, and a plain folder of recovered files are all directories, and
all fail for the same reason: what they expose is a *filesystem's* answer about
live files, not the bytes a filesystem was written into. One check covers every
spelling of the mistake, on both platforms, without this layer learning what a
UNC path looks like.

**An image *on* a share is still fine, and this must not break it.** ADR-0007 is
explicit that a `.dd` on `\\server\share\disk.img` supports full recovery — it is
a raw image, and the network is a latency problem rather than a capability one.
That path is a regular file, so it takes the image branch exactly as before. The
refusal is about the *share*, never about the network.

**It is refused where the device is opened, not where the flags are parsed.**
The CLI has no business knowing what makes a path readable; `openSource` already
answers that question for every caller, and putting the check anywhere else would
mean a second caller could still get a directory through.

**The sentence names the fix, not the failure.** "The source must be a disk image
or a device" is what an operator can act on; a code, or the OS's own complaint
about opening a directory, is not. It says what to do with a share as well,
because the person who typed one has a real disk somewhere behind it.

## Acceptance criteria

- [x] `openSource` on a directory yields `kNotBlockAddressable`, on both
      platforms, without touching the device layer.
- [x] `openSource` on a regular file is unaffected, including one that lives on
      a network path.
- [x] `describe(kNotBlockAddressable)` states that block-level access is needed
      and names what to point at instead.
- [x] `revenant-undelete --source <a directory>` fails, printing that sentence.

## Test plan

Unit (`tests/unit/io/SourceDeviceTest.cpp`): a directory is refused with the
typed code; a regular file still opens.

Unit (`tests/unit/cli/RunSummaryTest.cpp`): the sentence names block-level access
and mentions an image or a device.

Integration (`tests/integration/UndeleteCliTest.cpp`): a run whose `--source` is
a directory fails.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
