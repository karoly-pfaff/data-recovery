<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0011: The two halves of the read-only guarantee

**Status:** Accepted · **Date:** 2026-07-31 · **Clarifies:**
[ADR-0005](adr-0005-read-only-by-default.md)

## Context

[ADR-0005](adr-0005-read-only-by-default.md) states the guarantee this project is built
on: the source device is read-only by default and by construction. It states two things
under that one heading, and they are not equally strong. A survey of the tree found the
guarantee restated twenty-six times in four different strengths, and readers picking up
whichever restatement they met first had no way to tell which half they were relying on.

ADR-0005 stands as written. This record does not change the decision; it says which part
of it is a mechanical fact and which part is a check, so that a future reader trusts each
for the right reason.

## Decision

**By construction — true of the code as it stands.**

- Source handles are opened without write access (`GENERIC_READ`, `O_RDONLY`), through a
  single `openReadOnly` seam.
- `BlockDevice` declares no write operation, so no layer above the I/O boundary can
  express a write *to the source* at all.
- `tests/integration/SourceUnchangedTest.cpp` asserts that a full recovery over an image
  file leaves it byte-for-byte identical. It catches a write; it does not police the open
  flags, because relaxing those alone writes nothing. It covers `ImageFileDevice`;
  `RawDevice` has no equivalent run until
  [story-0603](../../backlog/stories/story-0603-linux-loop-device.md).

**Validated — only as good as the check behind it.**

- ADR-0005's destination rule ("on a different volume; the CLI validates this before
  starting") is enforced by a lexical path-prefix comparison in `RecoverySink`.
- That comparison does not hold for raw-device sources: `\\.\PhysicalDrive0` never
  prefixes `C:\recovered`, so a destination on the disk being recovered passes it.
  [story-0609](../../backlog/stories/story-0609-destination-on-source-refused.md) exists
  to make ADR-0005's sentence true as written.

**What "by default" excludes.** ADR-0005 requires any write-capable mode to be explicit,
guarded, opt-in, and to carry its own ADR. No such mode exists. Until one does, "by
default" and "never" describe the same behaviour, and other documents are entitled to say
"never" — provided they link here or to ADR-0005, so a reader can find which half they
are standing on.

## Consequences

- Documents restating the guarantee say "never" and link to the authority. They do not
  have to reproduce this split.
- A claim that the source "cannot" be written must mean the handle. A claim that output
  "cannot" land on the source is currently aspiration, not mechanism, and says so.
- When story-0609 lands, the Validated half becomes a real check and this record should
  say so.
