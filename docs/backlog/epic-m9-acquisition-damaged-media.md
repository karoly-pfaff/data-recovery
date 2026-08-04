<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M9 — Acquisition & damaged media

**Goal:** stop telling people to use another tool first. 1.0's own recovery playbook
opens by saying *image the failing drive before you touch it* — and then points at
`ddrescue`, because Revenant cannot do it. M9 closes that gap and makes the resulting
image a first-class citizen: one that carries the map of which bytes were never read,
into the machinery [M6](epic-m6-loose-ends.md) built to honour it.

**Milestone:** [M9](../roadmap.md#m9--acquisition--damaged-media)

## Outcome / definition of ready-to-close

- Revenant can acquire a failing device: forward-only, bad-sector-tolerant, resumable,
  emitting an image plus the map of what it could not read.
- `NetworkBlockDevice` reads a remote raw device (iSCSI, NBD) behind the same
  `BlockDevice` interface, per [ADR-0007](../architecture/adr/adr-0007-block-level-access-boundary.md).
- The playbook in `docs/` recommends Revenant's own imaging mode, and the
  `ddrescue` detour is deleted from it.

## Candidate stories (expanded when picked up)

| Story | Title | Size |
|-------|-------|:----:|
| *(unnumbered)* | Imaging mode: forward-only, bad-sector-tolerant acquisition + bad-sector map | L |
| *(unnumbered)* | Resumable acquisition — an imaging pass that survives being interrupted | M |
| *(unnumbered)* | `NetworkBlockDevice` (remote raw device: iSCSI/NBD) — ADR-0007 | L |
| *(unnumbered)* | Say the drive is dying before the six-hour run, not after | M |

**None of these carry numbers yet, and must not.** A number is allocated when a story
file is written, not when a milestone is sketched ([README.md](README.md#numbering)).
Two of them — the imaging mode and the remote device — were sketched in M4 and deferred;
under `story-MMNN` a deferred story does not carry its old milestone's number into a new
one, so they arrive here numberless like the rest. M9 numbers itself when M9 is picked
up, `story-08NN`, in whatever execution order it then has.

## What each story is

**Imaging mode.** A forward-only acquisition pass that reads the source
once, in device order, tolerating hardware faults instead of retrying them to death, and
writes an image plus a map of every range it could not read. Forward-only is the whole
point: [io-layer.md](../architecture/io-layer.md#acquiring-the-source-image-first) says
every extra read of a failing drive risks accelerating its death, so the acquisition
never seeks backwards to have another go — that is what the resume story is for, on
the operator's decision rather than the tool's.

It is also the first feature whose primary output is not recovered files, which raises a
question the story has to answer rather than assume: where does it live? A third binary
would need [ADR-0002](../architecture/adr/adr-0002-two-frontends-shared-core.md) amended;
a mode on an existing frontend would not. The answer belongs in the story, with an ADR if
it turns out to be the former.

**A hole is not a zero.** The deepest story in the milestone, and the one
that makes the rest worth having. An image acquired from failing hardware has gaps, and
if `ImageFileDevice` hands back zeros for them, everything above believes it read the
disk. A carver will happily validate a file whose middle is invented, or reject one that
is merely interrupted. So the bad-sector map travels *with* the image, the I/O layer
reports unknown ranges as unknown, and every consumer learns the difference: a candidate
that spans a gap is recorded as degraded rather than clean, and the manifest says which
of its bytes were never actually read. This is a precision feature, and precision over
recall is the project's founding claim
([ADR-0003](../architecture/adr/adr-0003-validating-carving.md)).

**Resumable acquisition.** An imaging pass over a terabyte of failing disk
will be interrupted — by the drive, by the operator, by the enclosure. Resuming has the
same shape as the resumable scan
([ADR-0008](../architecture/adr/adr-0008-resumability-checkpointing.md)) and should reuse
its machinery rather than grow a second one: a checkpoint that says how far the pass got
and what it has already given up on. It is separate from the acquisition story because a
forward-only pass that cannot resume is still useful, and a resume that predates a
working pass is not.

**`NetworkBlockDevice`.** A remote raw device — an iSCSI target, an NBD
export — behind the same `BlockDevice` interface, so nothing above the I/O layer learns
that the disk is on another machine.
[ADR-0007](../architecture/adr/adr-0007-block-level-access-boundary.md) already draws the
boundary this sits on and already rules out the thing people will confuse it with: a
file-level network share is not a recovery source, and story-0406 made the tool say so.

**Say the drive is dying first.** Before an operator spends six hours
imaging, the tool should say what the device reports about itself: reallocated sectors,
pending sectors, whether SMART thinks it is failing. It is platform work
(`IOCTL_STORAGE_QUERY_PROPERTY` on Windows, the SG/ATA path on Linux) and therefore
belongs in `core/io/` with the rest, per the M4 rule that platform code is confined and
CMake-selected rather than scattered in `#ifdef`. Advisory only: a drive that reports
itself healthy and is not is exactly the case recovery exists for, so nothing refuses to
run on this evidence.

## Notes

- **Read-only still holds.** Imaging writes to a destination, never to the source;
  [ADR-0005](../architecture/adr/adr-0005-read-only-by-default.md) is unchanged, and the
  acquisition path is subject to the same output-safety confinement as recovery
  ([ADR-0009](../architecture/adr/adr-0009-output-safety.md)).
- **Not in M9:** fragmentation-aware carving and exotic filesystems (APFS, Btrfs, XFS,
  ReFS). Both are named in the [roadmap](../roadmap.md#principles) as out of scope until
  a milestone pulls them in, and M9 does not: acquisition is about *getting* the bytes,
  and those two are about interpreting them. They remain unscheduled on purpose.
- **CodeQL** is not carried here: [M6](epic-m6-loose-ends.md) is where it lands, or it
  waits until after 1.0.
