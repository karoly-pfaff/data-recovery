<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0007: Block-level access boundary (incl. network sources)

- **Status:** Accepted
- **Date:** 2026-07-24

## Context

Users will point Revenant at sources reachable over a local network — for example an
image file on a file server, or a remote machine's disk. But "network access" spans two
fundamentally different capabilities, and conflating them leads to promising recovery we
cannot deliver.

Deleted-file recovery (undelete) and carving both require reading **raw blocks**:
filesystem metadata for deleted entries, and unallocated space for carving. A file-level
network share exposes only *live files* — never deleted entries, metadata, or free space.

## Decision

Revenant defines its source contract at the **block level**. The `BlockDevice` interface
is the single seam, and every source must provide random-access raw bytes:

- **Image files** (local or on a network share via UNC/mounted path) — supported now
  through `ImageFileDevice`. Being raw images, they permit full recovery; network latency
  and transient faults are absorbed by the `CachingDevice`/`RetryingDevice` decorators.
- **Physical disks and logical volumes** — supported through `PhysicalDevice` /
  `VolumeDevice`.
- **Remote raw devices** (iSCSI, NBD) — a future `NetworkBlockDevice` (M4+) behind the
  same interface.
- **File-level network shares** — explicitly **out of scope** as a recovery source.
  Revenant detects a file-level path used where a block source is required and rejects it
  with a clear message explaining that block-level access is needed.

## Consequences

- The capability boundary is honest and uniform: if it presents as a `BlockDevice`, full
  recovery works; if it is only a folder of files, it is refused with an explanation.
- Network image files need no special code path — they are just files, which keeps the
  I/O layer simple. Reliability, not capability, is the network concern, and it is handled
  by existing decorators.
- Remote raw-device support is a clean future extension, not a redesign.
- The destination may be a network path, but the CLI still enforces destination ≠ source
  and warns about unreliable destination storage.
