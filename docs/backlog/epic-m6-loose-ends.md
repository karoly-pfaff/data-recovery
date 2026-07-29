<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M6 — Loose ends & untested paths

**Goal:** pay what the earlier milestones borrowed. Every story here is a debt with a
name: an audit finding nobody acted on, a toolchain wart, a platform path that has only
ever been compiled, a bad-sector map that reaches nobody, a failure mode nobody has
provoked, and tests that never fit in a CI budget. None of it is new capability, and all
of it is between the toolkit and a 1.0.

**Milestone:** [M6](../roadmap.md#m6--loose-ends--untested-paths)

## Outcome / definition of ready-to-close

- No known audit finding is still open.
- Every gate script is Python; the repository needs no Node.js to build, test or gate.
- `RawDevice`'s Linux path has been *run*, against a real block device, privileged and
  unprivileged.
- A sector that could not be read is never silently reported as data: the bad-sector map
  reaches the manifest, and a candidate that spans one is marked.
- A run that loses its device, fills its destination, or cannot write its session ends
  with a usable partial result and says what happened.
- The parsers have seen hours of fuzzing, not twenty seconds, and memory has been proven
  bounded over a soak far longer than any test suite.

## Stories

| Story | Title | Size |
|-------|-------|:----:|
| story-0601 | Move `fs/SafeArith.hpp` to a neutral home | S |
| [story-0602](stories/story-0602-python-duplication-gate.md) | The duplication gate moves to Python, and Node.js leaves | S |
| story-0603 | The Linux device path, proven on a loop device | M |
| story-0604 | A hole is not a zero: the bad-sector map reaches the manifest and the candidates | L |
| story-0605 | A run that loses its device still ends with a usable result | M |
| story-0606 | Soak and a long fuzz campaign — the tests CI could never afford | M |

## What each story is

**story-0601 — `SafeArith` to a neutral home.** The M4 architecture audit's finding:
`fs::safeMul64`/`safeAdd64` are overflow-checked arithmetic over untrusted on-disk
numbers, not filesystem knowledge, and `volume/` became their second caller during M4.
The namespace is now a wart at those call sites. A move, not a redesign — which is
exactly why it waits for a quiet milestone rather than widening a feature story: it
touches `fs/ntfs`, `fs/fat`, `fs/exfat` and `volume/` at once, and wants a commit of its
own.

**story-0602 — the duplication gate moves to Python.** Every gate script here is Python
except the DRY detector, which is `jscpd` and brings Node.js, npm, a lockfile and 110
packages along for one check. `lizard`'s duplicate extension does the same job in pure
Python and finds *more* — it hashes unified tokens, so structurally identical but renamed
blocks are caught too, which is the clone class a knowledge-level DRY rule actually cares
about. Measured on the current tree: `jscpd` reports zero, `lizard` reports fifty. That
disagreement is the story's real work — the threshold has to be chosen and justified
rather than converted from "8 lines", and what it then reports has to be fixed or
explained, not tuned away.

**story-0603 — the Linux device path on a loop device.** `RawDevice`'s Linux half has
only ever been *compiled*; no test has run it, because CI runners do not hand out block
devices and Windows cannot pretend to be one. WSL2 can: `losetup` turns a synthetic
partitioned image into a real `/dev/loopN`. This story runs the whole stack against one —
open, size query, aligned reads, `--list-partitions`, a recovery — plus the unprivileged
case, which must produce the actionable error M4 promised rather than a bare `EACCES`. It
inherits the workbench [M5](epic-m5-performance.md) provisioned for `valgrind`.

**story-0604 — a hole is not a zero.** The most serious item in this milestone, and it
is a defect in shipped code rather than a missing feature. `RetryingDevice::readOneSector`
fills an unreadable sector with zeros, records a `BadRange`, and returns *success* — and
`badRanges()` has **no consumer anywhere in the tree**. Meanwhile the manifest's
`unreadable` list is populated from reads that *failed*, which, with a `RetryingDevice`
in the stack, is now none of them. So the decorator that exists to survive a bad sector
is precisely the one that erases it from the report: a carver validates a file whose
middle is invented, and it is written out as clean. This story connects the two ends —
the map reaches the manifest, and every consumer above the I/O layer can tell an
unreadable range from a range of zeros, so a candidate spanning one is recorded as
degraded rather than trusted. It is sized L because the answer touches `BlockDevice`'s
contract, and precision over recall is the project's founding claim
([ADR-0003](../architecture/adr/adr-0003-validating-carving.md)) — a tool that invents
bytes and does not say so breaks it.

**story-0605 — losing the device mid-run.** The commonest real-world failure of a
recovery run is that the drive goes away in the middle of it: a dying USB enclosure
resets, a failing disk stops answering. `RetryingDevice` handles a bad sector; it does
not answer what happens to a *run*. This story makes the answer explicit and tested
against the fault-injecting device: the partial result stays usable, the manifest records
what was lost and where, and the exit status distinguishes "finished" from "stopped
early". The destination filling up and an unwritable session directory get the same
treatment.

**story-0606 — soak and a long fuzz campaign.** Two things the 15-minute CI budget could
never hold. A soak run over a large synthetic image proves what "streaming, always"
claims — that memory stays bounded across hundreds of gigabytes and an interrupted run
resumes correctly from an arbitrary point. And an hours-long libFuzzer campaign per byte
parser, with whatever it finds triaged, fixed, and its inputs added to the curated
corpus. Both are one-off investments in a toolkit that will be pointed at other people's
damaged disks.

## Notes

- **CodeQL** lands here or nowhere before 1.0. It became free when the repository went
  public, it is a real fit for a C++ tool that parses hostile bytes, and this is the
  milestone with room for a new gate — [M5](epic-m5-performance.md) was the wrong place
  (a correctness gate in a performance milestone) and [M7](epic-m7-release.md) is worse
  (a new source of red runs in the milestone that tags the release). If it does not land
  here, it waits until after 1.0.
- **Where the line runs against [M8](epic-m8-acquisition-damaged-media.md).** M6 is what
  we built and got wrong or never proved; M8 is what we never built. Imaging mode, the
  remote device, resumable acquisition and drive health are new capability, and 1.0's
  limits page is allowed to say the tool does not have them. story-0604 moved the other
  way for the opposite reason: nothing about it is new, and leaving it means shipping a
  1.0 that fabricates bytes without saying so.
