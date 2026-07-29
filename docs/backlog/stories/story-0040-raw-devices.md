<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0040: Raw devices — whole disks and volumes, on both platforms

- Epic: [epic-m4-devices-partitions](../epic-m4-devices-partitions.md)
- Status: Done
- Size: M

## Goal

Promote from images to real media. Open a whole disk (`\\.\PhysicalDrive0`,
`/dev/sda`) or one of its volumes (`\\.\C:`, `/dev/sda1`) read-only, learn its
size and sector size from the OS, and read arbitrary byte ranges out of it — the
last of which a raw device will not do on its own.

## Design references

- [I/O layer](../../architecture/io-layer.md) — the `PhysicalDevice` /
  `VolumeDevice` rows, and the rule that "physical-device reads must be
  sector-aligned; the device aligns internally and slices the requested
  sub-range from an aligned read".
- [`ImageFileDevice`](../../../include/revenant/core/io/ImageFileDevice.hpp) and
  [`ReadRange.hpp`](../../../include/revenant/core/io/ReadRange.hpp) — the
  platform-split pattern this follows: CMake picks the native file, and every
  decision that is not a syscall lives in a header both compile.
- [ADR-0005](../../architecture/adr/adr-0005-read-only-by-default.md) — the
  handle is opened for reading only, on both platforms.

## Scope

1. **`RawDevice`** (`include/revenant/core/io/RawDevice.hpp`) — opens a device
   path read-only and reads arbitrary ranges from it.
2. **Alignment** (`include/revenant/core/io/AlignedRead.hpp`) — the
   platform-neutral logic that turns any request into the sector-aligned one the
   device will accept, and slices the answer back down. Header-only and pure, so
   it is the same code on both platforms and testable without a device.
3. **Privilege** — `ErrorCode::kPermissionDenied`, and the sentence the CLI
   prints for it.
4. **Native halves** — `RawDeviceWindows.cpp` (`CreateFileW`,
   `IOCTL_DISK_GET_LENGTH_INFO`, `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX`) and
   `RawDevicePosix.cpp` (`open`, `BLKGETSIZE64`, `BLKSSZGET`), each thin.

## Design decisions

**One class, not two.** The epic named a `PhysicalDevice` and a `VolumeDevice`,
and they turn out to be the same object: on Windows both are a `\\.\` path
opened with `CreateFileW` and measured with the same IOCTL, and on Linux both
are a path under `/dev` opened with `open(O_RDONLY)` and measured with the same
two ioctls. The only difference is which path the operator names — and this
layer's whole purpose is that the thing above it does not care. Two classes
would have been one implementation copied, which the duplication gate is right
to reject. `io-layer.md` now says so.

**The device aligns internally, and does not make its callers do it.**
`BlockDevice` promises that reads need not be aligned, and every parser in the
tree takes that promise: an MFT record is 1024 bytes at an arbitrary offset, a
directory entry is 32. A raw device on Windows refuses both. So the alignment
happens here, once, rather than in four filesystem parsers — and a request that
*is* already aligned is passed straight through to the caller's own buffer, so
the common case (a cache or a scan window above it) costs no copy at all.

**The alignment logic is a header, not a method.** It is the one part of this
story that can be tested without a disk, so it is written where a test can drive
it: `readThroughAlignment` takes the platform's aligned read as a callable, and
a test supplies one backed by a buffer that *refuses* unaligned requests — which
is exactly what the real device does, and what proves the slicing is right.

**A short aligned read is not a short answer of the same length.** The window
starts before the caller's offset, so the bytes the device managed to return
have to be measured from `skip`, not from zero: a read that stops one sector in
supplies nothing at all if the caller's range began after that. Getting this
backwards would hand back uninitialized bounce-buffer bytes.

**Insufficient privilege earns its own error code.** `ERROR_ACCESS_DENIED` and
`EACCES` are the single most likely thing to go wrong when an operator first
points this tool at a real disk, and "a required path does not exist" would send
them looking in the wrong place entirely. `kPermissionDenied` exists so the CLI
can say *run it elevated*, which is the only useful thing to say.

**Sector size is asked of the device, not assumed.** A 4Kn disk reports 4096,
and every alignment decision above depends on the answer being the device's
rather than a constant. Where the OS will not say — a query that fails on a
platform that has no such call for that object — the answer falls back to 512,
which is what every partition table's own arithmetic assumes anyway.

## Acceptance criteria

- [x] `alignedWindow` rounds an offset down and a length up to whole sectors,
      and reports how far into the window the caller's bytes begin.
- [x] An already-aligned request yields a window identical to it, with no skip.
- [x] A sector size of zero is treated as one, so the arithmetic never divides
      by it.
- [x] `readThroughAlignment` passes an aligned request straight through to the
      caller's buffer, making exactly one device read into it.
- [x] An unaligned request is served from an aligned window and yields the
      caller's own bytes.
- [x] A short aligned read yields only the bytes past `skip`, and none at all
      when the read stopped before the caller's range began.
- [x] A device fault propagates as a typed error.
- [x] `RawDevice::open` yields `kNotFound` for a path with no such device and
      `kPermissionDenied` when the OS refuses for want of privilege.
- [x] `RawDevice` reports the size and sector size the OS states, and reads
      arbitrary ranges out of a real device.
- [x] `describe` names the privilege failure with the action that fixes it.

## Test plan

Unit (`tests/unit/io/AlignedReadTest.cpp`): windows for an aligned request, one
offset into a sector, one whose length ends mid-sector, and one that spans three
sectors from the middle of the first; a zero sector size.

Unit (`tests/unit/io/AlignedReadTest.cpp`, continued): `readThroughAlignment`
driven against a reader that *refuses* anything unaligned — an aligned pass
through, an unaligned slice, a short read that reaches into the caller's range,
a short read that stops before it, and a propagated fault.

Unit (`tests/unit/cli/RunSummaryTest.cpp`): the privilege failure's sentence.

Not tested in CI: opening an actual device. A CI runner has no disk it may read
raw, and the story is built so that everything except the two syscalls is
exercised without one.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
