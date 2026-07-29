<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0402: `CachingDevice` and `RetryingDevice`

- Epic: [epic-m4-devices-partitions](../epic-m4-devices-partitions.md)
- Status: Done
- Size: M

## Goal

The two decorators the I/O layer has promised since M0: one that turns the many
small overlapping reads of parsing into few large device reads, and one that
survives a drive that will not answer. Both are `BlockDevice` wrappers, so they
compose with each other and with anything the source turns out to be.

## Design references

- [I/O layer](../../architecture/io-layer.md) — the decorator section, and the
  error model: a short read at end-of-device is a value, a fault is typed.
- [ADR-0007](../../architecture/adr/adr-0007-block-level-access-boundary.md) —
  network latency and transient faults are absorbed by these two rather than by
  a special code path per source.
- [ADR-0009](../../architecture/adr/adr-0009-output-safety.md) — the cache is
  sized by its own configuration, never by anything read off a device.

## Scope

1. **`CachingDevice`** (`include/revenant/core/io/CachingDevice.hpp`) — a
   least-recently-used cache of fixed-size, aligned blocks over another device.
2. **`RetryingDevice`** (`include/revenant/core/io/RetryingDevice.hpp`) — retries
   a failing read, then narrows to sectors, then hands back zeros for what is
   still unreadable and records the range.
3. **A fault-injecting device** (`tests/support/FaultyDevice.hpp`) — fails reads
   overlapping stated ranges, optionally only for the first few attempts, so
   both retry and give-up can be driven without failing hardware.

## Design decisions

**Neither decorator validates its way out of a bad configuration; it clamps.**
A block size that is not a power of two, or is smaller than a sector, is rounded
up, and a cache of zero blocks becomes a cache of one. This follows
`PartitionView`: these are *policy* parameters chosen by the code that composes
the stack, not untrusted numbers read off a disk, so a typed error would only
push a decision no caller can act on up the stack.

**The cache aligns every read it makes, which is the second thing it is for.**
Its stated purpose is coalescing, but a physical device on Windows *requires*
sector-aligned offsets and lengths ([story-0401](story-0401-raw-devices.md)),
and a cache that only ever reads whole aligned blocks satisfies that by
construction. Putting a `CachingDevice` over a raw device therefore buys
alignment as well as speed, and neither the filesystem parsers nor the carve
engine has to learn what a sector is.

**A cached block remembers how long it actually was.** The block covering the
end of the device is short, and storing the requested length instead of the read
one would hand back trailing zeros as if they were on the disk — the one thing a
recovery tool must never do.

**A fault is retried whole first, then a sector at a time.** A drive that fails
a 64 KiB request usually reads most of it on a second attempt, and when it does
not, the fault covers a few sectors rather than the request that happened to
span them. Retrying whole is what recovers a transient fault cheaply; narrowing
is what keeps a hard fault from costing the bytes on either side of it. Doing
only one of the two either gives up too much or costs too many attempts.

**The pause between attempts is a parameter, not a constant.** A dying drive's
own error recovery needs time, so the default is a real wait; a test needs none
and passes zero. There is no clock seam behind this: a duration that can be zero
is a simpler thing to test against than an injectable clock, and it is the
duration an operator would want to tune anyway.

**Zeros handed back are recorded, and adjacent records are merged.** A range
that could not be read is still returned to the caller as bytes — abandoning the
read would cost every file that merely *touches* the bad sector — so the fact
that those bytes are invented has to be recoverable, and `badRanges()` is where
it is kept. Merging adjacent ranges keeps a long bad run from becoming thousands
of one-sector records, which is the same bounded-allocation rule everything else
here follows. The session manifest's `unreadable` list is the natural consumer;
wiring it is the story that puts these decorators into a run.

## Acceptance criteria

- [x] `CachingDevice` reports the source's size and sector size, and reads
      identical bytes to the source for any offset and length.
- [x] A second read of the same range makes no further source read.
- [x] A read spanning two blocks is served from both.
- [x] A read past the end of the device is a short read, and the block covering
      the end holds only the bytes that were there.
- [x] The cache never holds more than `blockCount` blocks, and evicts the
      least recently used one.
- [x] A block size below a sector, or not a power of two, is rounded up; a block
      count of zero becomes one.
- [x] A source fault propagates as a typed error.
- [x] `RetryingDevice` returns the bytes a read that succeeds on its second
      attempt produced, having made exactly two source reads.
- [x] A range that fails every attempt is retried a sector at a time; sectors
      that read are the device's bytes, and sectors that do not are zeros.
- [x] Every zero-filled range appears in `badRanges()`, with adjacent ranges
      merged into one.
- [x] A read that never fails records nothing.
- [x] A `RetryingDevice` over a `CachingDevice` over a faulty source reads what
      it can and zero-fills what it cannot.

## Test plan

Unit (`tests/unit/io/CachingDeviceTest.cpp`): a full read matches the source; a
repeated read costs one source read; a two-block span; a read at the tail of the
device; eviction under a two-block cache; a clamped shape; a propagated fault.

Unit (`tests/unit/io/RetryingDeviceTest.cpp`): a transient fault that clears on
the second attempt; a permanent one narrowed to sectors, with the good sectors
intact and the bad ones zero; the merged bad-range record; a clean read
recording nothing; the two decorators stacked.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
