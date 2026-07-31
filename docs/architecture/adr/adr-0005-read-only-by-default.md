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
  source does not occupy; the CLI validates this before the first read, by physical
  identity rather than by path spelling. What "the source does not occupy" means depends
  on what the source is: a whole disk rules out every volume on it, a volume rules out
  itself but allows a sibling volume of the same disk, and a disk image is judged by its
  path — the output tree must not grow around the image it reads. A destination whose
  storage cannot be identified at all refuses the run rather than being assumed
  elsewhere.
- Write-capable repair features carry extra ceremony by design. This is intended.
