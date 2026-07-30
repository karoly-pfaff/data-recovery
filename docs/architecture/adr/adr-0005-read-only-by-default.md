<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0005: Read-only source by default

- **Status:** Accepted
- **Date:** 2026-07-24

## Context

Data recovery operates on media that is often already failing or holds the only copy of
irreplaceable data. Any write to the source risks overwriting the very clusters we are
trying to recover, and can turn a recoverable situation into a permanent loss.

## Decision

The source device is **read-only by default and by construction**. Three claims hide in
that phrase, and they are not equally strong; this record keeps them apart on purpose,
because conflating them is how the weakest gets trusted like the strongest.

**By construction — true now, and structurally hard to break:**

- Device handles are opened without write access (`GENERIC_READ` on Windows, `O_RDONLY`
  on Linux). It is not merely policy — the OS handle cannot write.
- `BlockDevice` declares no write operation, so no layer above the I/O boundary can
  express a write to the source even by mistake.
- No layer except the `RecoverySink` performs writes at all.
- A test asserts it: a full recovery leaves its source byte-for-byte identical
  (`tests/integration/SourceUnchangedTest.cpp`). A regression that opened the source
  read-write and touched it fails the build.

**Validated — enforced by a check, and therefore only as good as that check:**

- The sink writes only to a **separate destination** that must differ from the source.
  Recovered output requires a destination with sufficient free space, on a different
  volume; the CLI validates this before starting.
- **This half is not yet true for raw-device sources.** The check compares path
  spellings, and `\\.\PhysicalDrive0` never prefixes `C:\recovered`, so a destination on
  the disk being recovered passes it today.
  [story-0609](../../backlog/stories/story-0609-destination-on-source-refused.md) exists
  to make the sentence above true as written. Until it lands, the promise here is a
  design intent for this half and a mechanical fact for the other.

**By default — what the "default" excludes:**

- Any future feature that must write to a source (e.g. TestDisk-style partition-table
  repair) is a distinct, explicitly-guarded, opt-in mode with its own ADR, its own
  confirmation flow, and its own tests. It is never the default and never implicit.
- No such mode exists. Until one does, "by default" and "always" describe the same
  behaviour, and every other document in this repository is entitled to say "never".

## Consequences

- Running Revenant cannot damage the source through normal operation — the strongest
  possible guarantee for a recovery tool.
- Write-capable repair features carry extra ceremony by design. This is intended.
