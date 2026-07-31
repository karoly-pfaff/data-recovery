<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0005: Read-only source by default

- **Status:** Accepted
- **Date:** 2026-07-24

## Context

Data recovery operates on media that is often already failing or holds the only copy of
irreplaceable data. Any write to the source risks overwriting the very clusters we are
trying to recover, and can turn a recoverable situation into a permanent loss.

## Decision

The source device is **read-only by default and by construction**:

- Device handles are opened without write access (`GENERIC_READ` on Windows, `O_RDONLY`
  on Linux). It is not merely policy — the OS handle cannot write.
- No layer except the `RecoverySink` performs writes, and the sink only ever writes to a
  **separate destination** that must differ from the source.
- Any future feature that must write to a source (e.g. TestDisk-style partition-table
  repair) is a distinct, explicitly-guarded, opt-in mode with its own ADR, its own
  confirmation flow, and its own tests. It is never the default and never implicit.

## Consequences

- Running Revenant cannot damage the source through normal operation — the strongest
  possible guarantee for a recovery tool.
- Recovered output requires a destination with sufficient free space, on storage the
  source does not occupy. The CLI validates this before the first read, in two tiers. The
  first is path spelling and applies to every source: the output tree must not grow around
  the source it reads. The second applies only to a *device* source, which spelling cannot
  answer for at all — `\\.\PhysicalDrive0` shares no path element with `C:\recovered` — and
  compares physical storage: a whole disk rules out every volume on it, a volume rules out
  itself but allows a sibling volume of the same disk. A destination on a mapped or RAID
  volume is traced down to the disks it is built from, and one whose storage cannot be
  identified at all refuses the run rather than being assumed elsewhere. A destination that
  is not backed by a local block device — a network share — occupies no disk and so
  conflicts with nothing.
- Write-capable repair features carry extra ceremony by design. This is intended.
