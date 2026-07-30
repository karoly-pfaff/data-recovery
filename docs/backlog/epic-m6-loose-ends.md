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
- **A recovery run can no longer write onto the disk it is recovering.** ADR-0005's
  destination rule holds by physical identity rather than path spelling, for every kind
  of source.
- A sector that could not be read is never silently reported as data: the bad-sector map
  reaches the manifest, and a candidate that spans one is marked.
- A run that loses its device, fills its destination, or cannot write its session ends
  with a usable partial result and says what happened.
- The parsers have seen hours of fuzzing, not twenty seconds, and memory has been proven
  bounded over a soak far longer than any test suite.
- Every gate target *runs* on both development platforms; none is quietly CI-only.

## Stories

| Story | Title | Size |
|-------|-------|:----:|
| [story-0601](stories/story-0601-safearith-neutral-home.md) | Move `fs/SafeArith.hpp` to a neutral home | S |
| [story-0602](stories/story-0602-python-duplication-gate.md) | The duplication gate moves to Python, and Node.js leaves | S |
| [story-0603](stories/story-0603-linux-loop-device.md) | The Linux device path, proven on a loop device | M |
| [story-0604](stories/story-0604-bad-sector-map-to-manifest.md) | A hole is not a zero: the bad-sector map reaches the manifest and the candidates | L |
| [story-0605](stories/story-0605-device-loss-mid-run.md) | A run that loses its device still ends with a usable result | M |
| [story-0606](stories/story-0606-soak-and-long-fuzz.md) | Soak and a long fuzz campaign — the tests CI could never afford | M |
| [story-0607](stories/story-0607-format-gate-argument-list.md) | The format gate dies of its own argument list on Windows | S |
| [story-0608](stories/story-0608-namedecode-to-core.md) | The UTF-16 name decoder moves down to `core/`, and `volume/` stops depending on `fs/` | S |
| [story-0609](stories/story-0609-destination-on-source-refused.md) | A destination on the source disk is refused before the run starts | M |
| [story-0610](stories/story-0610-partition-scope-once.md) | Partition scope is decided once, in `recovery/` — and the table is read once per run | M |
| [story-0611](stories/story-0611-release-compiles-tests-clang-leg.md) | The release build compiles the tests, and clang gets an optimized leg | S |
| [story-0612](stories/story-0612-ci-runs-gate-targets.md) | CI runs the real gate targets on both platforms | S |
| [story-0613](stories/story-0613-layer-dag-gate.md) | The layer DAG becomes a gate: an upward include is a build failure | S |

story-0601 through story-0607 were the milestone as first scoped; story-0608 through
story-0613 come from the M5 architecture audit and are described under
[Stories added by the M5 architecture audit](#stories-added-by-the-m5-architecture-audit).

## What each story is

**story-0601 — `SafeArith` to a neutral home.** The M4 architecture audit's finding:
`fs::safeMul64`/`safeAdd64` were overflow-checked arithmetic over untrusted on-disk
numbers, not filesystem knowledge, and `volume/` became their second caller during M4,
which made the namespace a wart at those call sites. A move, not a redesign — which is
exactly why it waited for a quiet milestone rather than widening a feature story: it
touches `fs/ntfs`, `fs/fat`, `fs/exfat` and `volume/` at once, and wanted a commit of its
own. **Done:** they live at `src/core/SafeArith.hpp` in namespace `revenant`, still
internal, and all three guards are now pinned at both ends — which no test had held
before.

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

**story-0604 — a hole is not a zero.** The most serious item in this milestone. The M5
architecture audit corrected this paragraph's original premise: no shipped binary has a
`RetryingDevice` in its stack at all — `openSource()` builds only bare devices, both
decorators have zero production consumers, and their non-owning references cannot even be
composed onto the `unique_ptr` that `openSource()` returns. story-0402 deferred the
wiring on purpose; the 0.3.0 changelog and `io-layer.md` nonetheless describe it as done.
So today failed reads *do* propagate and *do* populate the manifest's `unreadable` list —
and the moment anyone wires `RetryingDevice` in without a consumer for `badRanges()`,
that list goes silent while `readOneSector` zero-fills unreadable sectors and returns
*success*: a carver validates a file whose middle is invented, and it is written out as
clean. This story therefore does both halves in order: build the owning composition that
puts the decorators into every real run, then connect the map — it reaches the manifest,
and every consumer above the I/O layer can tell an unreadable range from a range of
zeros, so a candidate spanning one is recorded as degraded rather than trusted. It is
sized L because the answer touches the device stack's contract, and precision over recall
is the project's founding claim
([ADR-0003](../architecture/adr/adr-0003-validating-carving.md)) — a tool that invents
bytes and does not say so breaks it.

**story-0605 — losing the device mid-run.** The commonest real-world failure of a
recovery run is that the drive goes away in the middle of it: a dying USB enclosure
resets, a failing disk stops answering. story-0604's composed stack handles a bad
sector; it does not answer what happens to a *run*. This story makes the answer explicit and tested
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

**story-0607 — the format gate dies of its own argument list.** Found by the first
full local gate run after M5 closed: `format-check` and `format` hand clang-format
every source file as one command line, and that line is now 32,997 characters against
Windows' 32,767-character limit — both targets fail before clang-format starts, on
every invocation. CI never noticed, because Linux's limit is megabytes; that is the
quiet way a local gate becomes a CI-only gate. A response file, batching, or a Python
driver in the story-0602 mold — the mechanism changes, the covered file set does not.
**Done:** it landed as the Python driver, batching under a stated budget, verified on
both platforms — and its self-audit turned up a copy of the file-length guard's
discovery code, so the two gates now share one answer to which files they cover.

## Stories added by the M5 architecture audit

The boundary audit ([code-quality.md](../code-quality.md), run 2026-07-30; summary in
[epic-m5](epic-m5-performance.md#milestone-architecture-audit)) confirmed seven findings
adversarially. One folded into story-0604's corrected scope above; the other six became
story-0608 through story-0613, and all three gate changes among them were approved into
this milestone rather than deferred past 1.0.

| Story | Finding it retires |
|-------|--------------------|
| story-0608 | `GptEntry.cpp` includes `fs/NameDecode.hpp` — the last upward edge in the DAG that story-0601 does not already own. ADR-0010 gains the new seam. |
| story-0609 | ADR-0005's guard is a lexical path-prefix check from the image-file era; a raw-device source (`\\.\PhysicalDrive0`, `/dev/sda`) never matches it, so recovery output can land on the disk being recovered. The highest-stakes finding of the audit. |
| story-0610 | `cli/` resolves the partition and builds the view; `enumerateDisk` then re-reads the table *inside* it, three probes deep, and weak MBR validation lets a phantom table through. |
| story-0611 | Three latent-bug instances found only by first-ever builds in untried configurations; today no test TU compiles at `-O2 -Werror` anywhere and no optimized clang build exists. |
| story-0612 | `format-check` died on every Windows invocation and nothing noticed until after a release; the checks developers run locally are reimplemented in bash in CI rather than invoked. |
| story-0613 | The inversion shipped through review and every PR since, because nothing checks include direction. |

**Three findings did not survive contact with their own story.** Each author verified the
audit's anchors before scoping from them, and three claims came back narrower than the
audit put them — which is the point of writing the story before the code:

- **story-0608.** `decodeUtf16Name` holds *no* path policy: `/` and `%` walk straight
  through it, and it never calls the escaping predicate at all. What couples a GPT label
  to `fs/` is the address and the escape *spelling*, not ADR-0010's path rules. A cleaner
  seam than the finding predicted.
- **story-0610.** A phantom nested table does not walk bogus sub-windows. Every phantom
  window clamps to length zero, fails to mount, and is swallowed — and `enumerateDisk`
  still returns a *value*, so the run records `mounted`, zero entries, and nothing
  non-conforming. The hazard is worse for being quiet: an undelete of an intact volume
  silently degrades to carve-only while both "something was wrong" flags say nothing was.
- **story-0612.** "CI invokes the real gate targets on no platform" was one target too
  broad — the `tidy` target *is* invoked on ubuntu, fail-fast shard validation included.
  It is the existence proof for the pattern, not a counterexample. And the bash
  reimplementation selects the same files as the target does today, so the coverage gap
  is latent rather than actual.

Lower-severity observations the audit passed through unverified are recorded in the
[M5 audit note](epic-m5-performance.md#milestone-architecture-audit); the story authors
of 0604 and 0605 already fold in the two that touch them (`WindowMatch.cpp`'s split, the
frontend's one-bool outcome). The remaining ones — `RecoveryOptions.cpp`'s flag-clone
family, `SignatureScanner.hpp`'s embedded internals, the unwritten fast-path ADR, and
ADR-0007's stale taxonomy — are checked when their neighborhood is next opened, not
queued as stories on an unverified say-so.

## Notes

- **Ordering the audit's stories imposed.** story-0610 goes before story-0604, which
  goes before story-0605: 0604 pins its device-absolute bad-range translation to the very
  lines 0610 deletes, and 0605 needs 0604's composed stack to have something to give up
  on. story-0613's gate lands only after both cures (story-0601 and story-0608) have
  removed the upward edges — with no allowlist, because a burn-down list whose one entry
  is owned by a story in the same milestone would be a worse copy of the table above.
  story-0612 depends on story-0607, which made the Windows format target invokable at all.
  Two stories extend `ErrorCode` independently — story-0605 adds source-lost and
  storage-exhausted, story-0609 adds destination-on-source — so whichever lands second
  rebases onto the first rather than inventing a parallel taxonomy.
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
  1.0 with either no bad-sector tolerance at all or one that fabricates bytes without
  saying so.
