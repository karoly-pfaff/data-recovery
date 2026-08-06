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
  unprivileged. **Done** (story-0603): against a `losetup` device on the Linux workbench,
  with the image-file run over the same bytes as the oracle, at 512-byte and 4096-byte
  geometry, and refused as a user who genuinely lacks permission. Nothing was broken.
- **A recovery run can no longer write onto the disk it is recovering.** ADR-0005's
  destination rule holds by physical identity rather than path spelling, for every kind
  of source. **Done** (story-0609): the destination is compared to the source by disk
  extents on Windows and, on Linux, traced through the mount table and the kernel's
  `slaves` links to the disks underneath — so btrfs, LVM, LUKS and md destinations are
  caught, where a filesystem's own device number would have misled. A sibling volume
  stays allowed on purpose. Two containers are not traced through and are recorded for
  the 1.0 limits page in [epic-m7](epic-m8-release.md#notes).
- A sector that could not be read is never silently reported as data: the bad-sector map
  reaches the manifest, and a candidate that spans one is marked. **Done** (story-0604):
  `openSource` returns an owning `SourceStack` and no bare-device path is left, so every
  run has a map; it reaches the manifest as `{offset, length}` ranges and every artifact
  carries its own overlap under `invented`, device-absolute in a `--partition` run too.
  Composing the decorators also cost 42% of carve throughput until the cache came back
  out of the stack, and flushed out a map that was a log of read events rather than a set.
- A run that loses its device, fills its destination, or cannot write its session ends
  with a usable partial result and says what happened. **Done** (story-0605): exit codes
  0–4 replace one bool, a megabyte of contiguous unreadable source is a lost device rather
  than more zero-fill, and every ending that produced anything writes a manifest saying
  how far it got and where it stopped. The give-up bound is a choice and can misfire on a
  very large real defect, which leaves the run stuck rather than merely stopped; that
  limit is recorded in the story.
- The parsers have seen hours of fuzzing, not twenty seconds, and memory has been proven
  bounded over a soak far longer than any test suite. **Done** (story-0606), and the
  fuzzing half found something worse than a parser bug on its way in: `-fsanitize=fuzzer`
  had been applied to each fuzz target and to nothing it links, so `librevenant` — every
  parser in the project — carried no coverage instrumentation at all. Every campaign, and
  the fuzz gate on every push, had been mutating with feedback from the harness file
  alone. The library now gets `-fsanitize=fuzzer-no-link`, coverage from the *same*
  committed corpora rose 5× to 14× per target, and gate 13 fails a build that loses it
  again. The soak measured what it set out to: 72.1 MiB peak over 128 MiB and 72.1 MiB
  over 256 GiB — a 2,048× larger device for 0.0% more memory — and an interrupted run
  resumed to a manifest identical to its control across all 256 planted files.
- Every gate target *runs* on both development platforms; none is quietly CI-only.
  **Partly done** (story-0612), and the remainder is named rather than left implied:
  `format-check` and `guard-limits` are invoked as the literal targets on both platforms,
  which is where the milestone's failure actually was. `tidy` stays Linux-only in CI
  because clang-tidy cannot parse the MSVC ASan + `/MDd` combination, so running it on
  Windows means a second configure and an entire extra optimized build — it is run from
  the `release` preset locally, and that is prose, not a machine check. Duplication,
  coverage, fuzz and encoding stay Linux-only because their questions have no platform
  dimension. The per-gate table is in
  [quality-gates.md](../testing/quality-gates.md#which-job-runs-which-gate-and-where).

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
| [story-0614](stories/story-0614-docs-one-job-each.md) | One job per document, and the read-only guarantee checked by a test | M |
| [story-0615](stories/story-0615-codeql-code-scanning.md) | CodeQL reads the parsers the way an attacker would | S |

story-0601 through story-0607 were the milestone as first scoped; story-0615 is the
CodeQL decision the notes below had left open; story-0608 through
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
explained, not tuned away. **Done:** the threshold is 60 tokens per copy — the median
function in the scanned files is 62 — and a block counts only where every copy of it
reaches a function body, because a run of layout constants hashes like any other and
every byte parser here has one. Node.js, `package.json` and the lockfile are gone.

**story-0603 — the Linux device path on a loop device.** `RawDevice`'s Linux half has
only ever been *compiled*; no test has run it, because CI runners do not hand out block
devices and Windows cannot pretend to be one. WSL2 can: `losetup` turns a synthetic
partitioned image into a real `/dev/loopN`. This story runs the whole stack against one —
open, size query, aligned reads, `--list-partitions`, a recovery — plus the unprivileged
case, which must produce the actionable error M4 promised rather than a bare `EACCES`. It
inherits the workbench [M5](epic-m5-performance.md) provisioned for `valgrind`. **Done:**
ten checks, all green, no defect found — and because the first full run passed
everything, the harness's own pass/fail logic is unit-tested in CI and was
mutation-checked, since an identity with nothing in it reports exactly what a real one
does. Two of the story's claims did not survive being measured: at `--sector-size 4096`
the kernel rescales the MBR's LBAs, so its partition scan stops being a second witness;
and `manifest.json` records the source and destination paths, so the recovery identity is
an artifact tree plus a field-wise manifest comparison rather than one `diff -r`.

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
bytes and does not say so breaks it. **Done:** `openSource` returns an owning
`SourceStack` and there is no bare-device path left; the map reaches the manifest as
`{offset, length}` ranges and every artifact carries its own overlap under `invented`,
device-absolute in a `--partition` run as well. Composing the decorators also flushed out
a defect they had been carrying unused: the map was a log of read events rather than a
set, so a sector met twice — once scanning, once extracting — would have been reported
twice, and no fixture could see it because the NTFS image is exactly the size of the
cache.

**story-0605 — losing the device mid-run.** The commonest real-world failure of a
recovery run is that the drive goes away in the middle of it: a dying USB enclosure
resets, a failing disk stops answering. story-0604's composed stack handles a bad
sector; it does not answer what happens to a *run*. This story makes the answer explicit and tested
against the fault-injecting device: the partial result stays usable, the manifest records
what was lost and where, and the exit status distinguishes "finished" from "stopped
early". The destination filling up and an unwritable session directory get the same
treatment. **Done:** exit codes 0–4 documented in `README.md` and both `--help` texts; a
megabyte of contiguous unreadable source is a lost device rather than more zero-fill,
which closes the trap story-0604 opened; a full destination and a session that stops
taking writes each stop the run; and every ending that produced anything writes a
manifest carrying `outcome`, `scannedUpTo` and where a lost device stopped answering.
The give-up bound is a choice and can misfire on a very large real defect, which leaves
the run stuck rather than merely stopped — recorded in the story, and a flag for it is
work of its own.

**story-0606 — soak and a long fuzz campaign.** Two things the 15-minute CI budget could
never hold. A soak run over a large synthetic image proves what "streaming, always"
claims — that memory stays bounded across hundreds of gigabytes and an interrupted run
resumes correctly from an arbitrary point. And an hours-long libFuzzer campaign per byte
parser, with whatever it finds triaged, fixed, and its inputs added to the curated
corpus. Both are one-off investments in a toolkit that will be pointed at other people's
damaged disks. **Done**, and the campaign's most valuable finding was made before it
started: the fuzzers had no coverage feedback from the code under test, which made the
number of hours the least important variable in the story. The campaign was also scoped
down from 112 CPU-hours to 14.5 by decision, and the story records what that buys and
what it gives up rather than rewriting the criterion to match. The soak needed a
generator verb that did not exist — `pattern` streams but plants nothing, `carve` plants
files but builds the whole image in memory — so `soak` is the third, and a ctest asks the
OS whether it really streams, because a generator that buffers passes every other test.

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
| story-0610 | `cli/` resolves the partition and builds the view; `enumerateDisk` then re-reads the table *inside* it, three probes deep, and weak MBR validation lets a phantom table through. **Done:** scope is resolved once, in `recovery::RunScope`, and the walk is handed the partitions it mounts. `Mbr.cpp` is untouched on purpose — anything that rejected bootstrap code would reject the real table on a truncated image. |
| story-0611 | Three latent-bug instances found only by first-ever builds in untried configurations; today no test TU compiles at `-O2 -Werror` anywhere and no optimized clang build exists. **Done:** a fourth instance turned up the moment the configuration existed — a fixture that sized its buffer from a length *field* and then wrote past it — plus the same four-line file reader copied into three test files. The release job compiles the tests and asserts it did; a clang leg compiles them optimized and was clean on arrival. |
| story-0612 | `format-check` died on every Windows invocation and nothing noticed until after a release; the checks developers run locally are reimplemented in bash in CI rather than invoked. **Done:** both jobs invoke the literal targets, on both platforms, off a 5-second configure that resolves no dependency. Watched failing twice on purpose — and the criterion asking for both jobs red at once turned out to describe a state the workflow cannot enter, because one gates the other. |
| story-0613 | The inversion shipped through review and every PR since, because nothing checks include direction. **Done:** `check_layering.py` joins `guard-limits`, with no allowlist — the two cures emptied its list first, so it went green on arrival. Run against `5315704` it names all three edges of that era, one of them written with the `revenant/` prefix and two without. |

**Three findings did not survive contact with their own story.** Each author verified the
audit's anchors before scoping from them, and three claims came back narrower than the
audit put them — which is the point of writing the story before the code:

- **story-0608.** `decodeUtf16Name` holds *no* path policy: `/` and `%` walk straight
  through it, and it never calls the escaping predicate at all. What couples a GPT label
  to `fs/` is the address and the escape *spelling*, not ADR-0010's path rules. A cleaner
  seam than the finding predicted. **Done:** the transform and the escape spelling live at
  `core/`, the path rule stayed in `fs/` as `PathSafeByte`, and no layer depends upward on
  another any more — which is what story-0613's gate was waiting for. The seam had one
  cost the story did not predict and the audit caught: `%` was reserved and emitted in one
  file, so splitting it needed the sigil named once rather than copied.
- **story-0610.** A phantom nested table does not walk bogus sub-windows. Every phantom
  window clamps to length zero, fails to mount, and is swallowed — and `enumerateDisk`
  still returns a *value*, so the run records `mounted`, zero entries, and nothing
  non-conforming. The hazard is worse for being quiet: an undelete of an intact volume
  silently degrades to carve-only while both "something was wrong" flags say nothing was.
  Two more things the story did not predict, both found by its own audit. The
  single-volume fallback did not move whole: its premise went to the resolver and its
  branch stayed a step above the walk, and what the story actually buys is that the
  table read left the walker. And the cure reaches only as far as the operator's answer
  does — a whole-source run over a single real volume still asks it whether it is a
  disk, which is the residual recorded under [Notes](#notes).
- **story-0612.** "CI invokes the real gate targets on no platform" was one target too
  broad — the `tidy` target *is* invoked on ubuntu, fail-fast shard validation included.
  It is the existence proof for the pattern, not a counterexample. And the bash
  reimplementation selects the same files as the target does today, so the coverage gap
  is latent rather than actual.

One more, found while writing story-0614 rather than by the audit:
`tests/integration/ArbitratedRecoveryTest.cpp` still assembles its own copy of the
full-recovery run, which `tests/support/RecoveryPipeline` now owns for the other two
integration tests. Folding it in needs a second entry point, because its seam is
arbitration rather than extraction. No gate will catch it — the duplication detector
scans `src include tools` only.

Lower-severity observations the audit passed through unverified are recorded in the
[M5 audit note](epic-m5-performance.md#milestone-architecture-audit); the story authors
of 0604 and 0605 already fold in the two that touch them (`WindowMatch.cpp`'s split, the
frontend's one-bool outcome). The remaining ones — `RecoveryOptions.cpp`'s flag-clone
family, `SignatureScanner.hpp`'s embedded internals, the unwritten fast-path ADR, and
ADR-0007's stale taxonomy — are checked when their neighborhood is next opened, not
queued as stories on an unverified say-so.

One more, produced by story-0611 rather than by the audit, and recorded here so it is not
left in a story's prose: **`include/revenant/core/Result.hpp`'s comment on `map` and
`andThen` may no longer describe a live constraint.** It says the `hasValue()`-then-
`get_if` shape "leaves the compiler looking at an unguarded dereference … which GCC
reports as `-Wnull-dereference` once the optimizer inlines it", and that is why the
functions ask by pointer instead. story-0611 put that shape back and built the tree
optimized: GCC 14.2.0 recompiled 257 objects and stayed green. The comment was true of the
GCC that found the bug in `c2e8da0`; whether it is still true of the GCC that gates merges
is unmeasured. The current shape is correct either way, so nothing is broken — but the
*reason* recorded for it is a fact with an expiry date, and the next person to edit those
functions should re-measure rather than trust the comment.

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
- **A whole-source run over a single real volume still asks it whether it is a
  disk** — story-0610's residual, named there and repeated here so it is not
  left in a story's prose. `--source /dev/sda1` with no `--partition` gives the
  resolver nothing to work from but the volume's own sector 0, and a real
  volume's bootstrap area parses as a table; the run then walks phantom
  partitions, mounts none, and reports a healthy filesystem with no files in it.
  Unchanged by story-0610 (`enumerateDisk` did the same before it) and outside
  its scope: the cure is a decision — walk the device as a volume when nothing
  its table names will mount, or ask the OS what kind of thing it handed over.
  Either the 1.0 limits page in [epic-m7](epic-m8-release.md#notes) says the
  tool wants `--partition` for a volume source, or it becomes a story.
- **CodeQL lands here**, as [story-0615](stories/story-0615-codeql-code-scanning.md).
  That story now owns what this note used to argue — why M5 and the release milestone were both worse
  places for it, and why it arrives reporting rather than gating.
- **CodeQL cannot see this project's read path** — story-0615's residual, named there and
  repeated here so it is not left in a story's prose. The taint queries work: a planted
  unvalidated length reaching `malloc` was reported at `high`, three times. But the planted
  flow started at `std::fread`, and this tree has none: it reads through `::pread`,
  `::ReadFile` and `std::ifstream`, for which CodeQL's C++ library declares no flow-source
  model at all (it models `fread`, `getdelim`, `gets`, `scanf`, `recv` and the socket
  functions). So `core/io/` is not a taint source and no configuration of the shipped
  queries makes it one — `threat-models: [ local ]` was tried and changed nothing. The cure
  is a decision: either a CodeQL model pack naming `readAt` and its platform primitives as
  remote sources, which is the only thing that would make gate 12 answer the question
  story-0615 set it, or the 1.0 limits page says that static taint analysis covers the
  parsers' arithmetic and not the device boundary. A story either way — it is not a defect
  in what landed, which is a working analysis with a documented blind spot.
- **Where the line runs against [M9](epic-m9-acquisition-damaged-media.md).** M6 is what
  we built and got wrong or never proved; M9 is what we never built. Imaging mode, the
  remote device, resumable acquisition and drive health are new capability, and 1.0's
  limits page is allowed to say the tool does not have them. story-0604 moved the other
  way for the opposite reason: nothing about it is new, and leaving it means shipping a
  1.0 with either no bad-sector tolerance at all or one that fabricates bytes without
  saying so.

## Milestone architecture audit

Run at the milestone boundary, per [code-quality.md](../code-quality.md), as the
multi-agent adversarial pass the `milestone-audit` skill defines, over `v0.3.1..HEAD`.
Four findings survived refutation; four were refuted — three of them the layer-leakage
claims, which is itself the headline; eight lower-severity observations go to M7 story
generation unverified.

- **Did a layer leak?** **No — the first clean answer this audit has produced**, and
  story-0613's gate is why. Three leakage claims were raised and all three refuted.
  `cli/` including `src/recovery/DestinationRule.hpp` and `src/recovery/Damage.hpp` is a
  *downward, adjacent* edge the DAG permits by construction
  (`tools/lint/check_layering.py:41`), and sharing an internal header across layer
  directories is the documented *purpose* of a `src/`-rooted header
  (`src/CMakeLists.txt:215-216`, written in M1), not a new hole in it — 13 such edges
  exist today and the practice predates the range. The decide-record-report composition
  now split across `RunManifest`/`RunOutcome`/`RunDamage` was already in `cli/` before
  M6: `v0.3.1:src/cli/RunDelivery.cpp` assembles `SessionManifest` field by field, so the
  increment divided responsibility that was there rather than moving it up. And
  `core/io`'s new identity subsystem is mechanism below policy — the leak would have been
  sysfs walking inside `recovery/`. One residual, low and unverified: the run's two
  coordinate systems are reconciled twice, `recovery::placedOn`
  (`src/recovery/Damage.cpp:26-29`) and `cli::onTheDevice` (`src/cli/RunDamage.cpp:29-37`)
  each adding `startBytes`, correct only because `src/cli/RunDelivery.cpp:199` nests them
  in an order neither header states; reverse them and a scoped run over a failing disk
  reports no damage at all.

- **Did the interfaces hold?** The code holds. **The record of it does not, and the
  breach is in the one document whose whole job is telling a reader which half of the
  read-only guarantee is mechanism and which is a check.** ADR-0011 is `Accepted` and
  still says the destination rule "is enforced by a lexical path-prefix comparison in
  `RecoverySink`" which "does not hold for raw-device sources", naming story-0609 as work
  that "exists to make ADR-0005's sentence true as written" (ADR-0011's *Validated* half);
  its Consequences still call the claim "aspiration, not mechanism" and instruct "When
  story-0609 lands, the Validated half becomes a real check and this record should say
  so" (its third Consequence). story-0609 landed at `4a4221e`; the rule lives in
  `recovery::destinationOnSource` — spelling tier, then physical identity over
  `StorageExtents` — and ADR-0011 was *itself edited afterwards* at `6111268` with the
  stale half three lines below left untouched. So reality did demand a new seam (two
  tiers, `DeviceIdentity`, refuse-on-unresolvable, a named Storage-Space/VHD gap) and it
  got an edit instead of a record: `4a4221e` rewrote ADR-0005's Consequences in place
  (+14/−2), which `adr-0001:19` forbids and which the ADR index added in
  this same increment restates verbatim (`adr/README.md:7`). There is no ADR-0012. M8's
  limits page is written off documents whose authority is now ambiguous.

  **Resolved by [story-0701](stories/story-0701-adr-0012-destination-rule.md) (2026-08-06):**
  ADR-0012 now exists, ADR-0011's Validated half and its destination Consequences carry
  supersession markers, and ADR-0005's Consequences are back to the accepted text. The
  finding above is left as written — it is the audit's record of what was true on
  2026-08-04 — but its line-number citations were re-anchored to section and symbol names,
  because story-0701's own edits to ADR-0011 moved them. That is the class
  [story-0706](stories/story-0706-citations-resolve.md) exists to gate.

- **Did complexity creep in?** Yes — in the two surfaces nothing measures. The **CLI
  surface is owned in four places and restated in three more**: constants at
  `src/cli/RecoveryOptions.cpp:21-27`, of which only four of seven reach `kSharedFlags`
  (`:116-120`) while the path flags stay in `pathFieldOf` (`:30-41`) and each frontend
  adds its own (`CarveOptions.cpp:19`, `UndeleteOptions.cpp:16-18`), then hand-written
  help strings (`CarveCli.cpp:18-24`, `UndeleteCli.cpp:17-22`), four header comments, and
  `docs/usage.md`. It has already drifted: `--help` is a real accepted flag
  (`src/cli/Frontend.cpp:25`, matched at `:38`, acted on at `:102`) documented in neither
  usage text, and `--force-portable` is in both help texts but absent from
  `docs/usage.md:11-13,31-33`. The fix pattern is in the same file — `usage()` renders
  format names *from the carve layer* (`CarveCli.cpp:26-31`) "so the help can never offer
  a name the allowlist would then refuse". Second: **`tools/` is handed to two gates that
  measure only its C++.** `SOURCE_SUFFIXES = {".cpp", ".hpp"}` (`tools/lint/source_set.py:26`)
  is the one answer to gate coverage, while `check_file_length.py` and
  `check_duplication.py` both take `${CMAKE_SOURCE_DIR}/tools` as a root
  (`cmake/DevTargets.cmake:123,136`). Under that cover M6 moved 2,115 new lines of Python
  into the blind spot — 13 files/1,681 lines at `v0.3.1` to 28/3,796 at HEAD — including
  the largest source file in the tree, `tools/fuzz/make_seed_corpus.py` at 763 lines and
  50 top-level definitions, 3× the hard fail, restating filesystem and carve-format
  layouts the C++ fixtures already hold and documenting its own decay path at `:576-583`.
  The exclusion is deliberate and unit-tested, so closing it is an AGENTS.md §2 scope
  decision, not a bug fix.

- **Anything to automate?** Three classes, each with its instance count. **Vacuous gates:
  six instances in this milestone alone** — the empty-set refusal is a convention copied
  into five scripts (`check_coverage.py:60`, `check_duplication.py:126`,
  `check_encoding.py:93`, `check_format.py:61`, `check_layering.py:149`), `source_set.py:11-13`
  says so in its own docstring, and the sixth walking gate has neither the guard nor a
  test: `check_file_length.py:32-48` returns 0 over an empty set, and it is the only lint
  script in `tools/lint/` with no unit test — so gate 3, which enforces the AGENTS.md §2
  headline number, has nothing proving it ever goes red, and it was edited inside this
  range while its siblings gained guards. Five more bespoke one-offs answer the same class
  (`check_fuzz_instrumentation.py:62-72`, the tidy shard FATAL_ERROR at
  `cmake/DevTargets.cmake:77-90`, story-0611's binary-exists assertion, story-0615's
  CodeQL archive set difference, story-0603's mutation harness). **ADRs edited rather than
  superseded:** breached once, in the same commit range that documented the rule.
  **Stale `path:line` citations in story docs:** fixed by hand four times in this range
  (`b429328`, `ce85499`, `722e12a`, `aa80d97`), with six citations provably past EOF at
  HEAD and 162 surviving across 14 story files; nothing in AGENTS.md, code-quality.md or
  CONTRIBUTING.md says to cite by name. Four gate proposals go to the maintainer.

- **Findings become stories:** four — an ADR-0012 recording the destination rule as
  decided with ADR-0005 restored and ADR-0011 corrected; one flag table the parser and
  `--help` both read, before M8's documentation story writes man pages off a list that is already wrong;
  the gates measuring the Python they are handed, with the seed generator split; and the
  vacuity refusal moving from convention into `gate_files`.

**Lower-severity observations passed through unverified**, for M7 story generation rather
than queued on an unverified say-so: ADR-0008:20-22 names the bad-sector map as durable
session state but `Checkpoint.hpp:18-36` persists only `shape`/`scanCursor`/`indexRecords`,
so a resumed run's manifest reports only its own pass; ADR-0007:24-26 justifies network
sources by decorators that `SourceStack::over` no longer composes
(`include/revenant/core/io/SourceStack.hpp:31`), leaving `CachingDevice` as production-dead
public API, and ADR-0007:29 still schedules `NetworkBlockDevice` for M6;
`src/cli/RunDelivery.cpp` at 227/250 lines threading a four-value clump through ten
functions; `RecoverySink`'s seven responsibilities and its single-use accumulator
(`RecoverySink.hpp:99-155`, `return std::move(result_)`); stale line-number citations;
`tests/` sitting outside gates 3 and 4 while AGENTS.md:49 and quality-gates.md:28-29 claim
otherwise (eight test files over 250 lines, duplication at 15.66%); the double coordinate
restatement above; and nine copies of the toolchain pins outside `ci.yml`'s env entries.
