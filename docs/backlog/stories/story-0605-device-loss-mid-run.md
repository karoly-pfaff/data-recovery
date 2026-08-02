<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0605: A run that loses its device still ends with a usable result

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: In review
- Size: M

## Goal

The commonest real-world failure of a recovery run is that the drive goes away in the
middle of it: a dying USB enclosure resets, a failing disk stops answering. Today that
ends in one generic stderr line, a silently usable session nobody is told about, and the
same exit code as a mistyped flag. Make the three ways a run dies — source gone,
destination full, session unwritable — end well *on purpose*: every already-recovered
file stays valid, the manifest records what was lost and where, the exit status
distinguishes "finished" from "stopped early", and the run's last words say what to do
next.

## Design references

- [story-0604](story-0604-bad-sector-map-to-manifest.md) — wires `RetryingDevice` into
  the production stack and gives the bad-sector map its path to the manifest. This story
  runs after it, on the composed stack, and states why below.
- [ADR-0008](../../architecture/adr/adr-0008-resumability-checkpointing.md) — "re-reading
  a dying disk can hasten its death"; the session directory this story reports into.
- [story-0117](story-0117-resumable-scan.md) — the checkpoint/resume machinery a stopped
  run must leave valid; this story connects to it and duplicates none of it.
- [story-0115](story-0115-session-manifest.md) /
  [recovery-output.md](../../architecture/recovery-output.md) — what the manifest records
  today, and what it promises that the code does not yet keep.
- [story-0402](story-0402-io-decorators.md) — `tests/support/FaultyDevice`, the
  fault-injecting device this story extends and tests against.
- [story-0406](story-0406-reject-file-shares.md) — the actionable-error standard M4 set:
  a failure names itself and the next step, not an error code.
- [versioning.md](../../versioning.md) — CLI behaviour is covered by SemVer from 1.0.0.
  Exit codes are CLI behaviour; this story's scheme is the one that freezes.

## What was measured

**The whole outcome is one bool.** `report()` folds could-not-run, ran-but-stopped and
finished into a single `return outcome.value().discovery.scanComplete;`
(`src/cli/Frontend.cpp:47-56`, the fold at `:55`), and that bool is all
`recover`/`perform`/`runFrontend` (`:70-101`) hand to the mains, which map it to 0 or 1
(`src/cli/CarveMain.cpp:9`, `src/cli/UndeleteMain.cpp:9`). A mistyped flag, a refused
destination, a device that died at byte forty billion, and an operator's `Ctrl-C` all
exit 1. No exit-code table is documented anywhere; `README.md:127-131` promises only
"non-zero".

**Source gone.** A failed read in the carve pass propagates untouched:
`src/carve/SignatureScanner.cpp:88-91` → `src/recovery/HybridRecovery.cpp:125-128` →
the region loop breaks (`:141-146`) → `run()` returns the error (`:173-185`) →
`recoverInto` gives up before arbitration (`src/cli/RecoveryRun.cpp:133-136`). No
manifest, no summary — one line: "a read or write failed; the run stopped rather than
report a smaller world" (`src/cli/RunSummary.cpp:98-99`), which drops even the offset
the `Error` carries. What *survives* is actually good: the index is flushed per append
(`src/recovery/CandidateIndex.cpp:136-149`) and the checkpoint stands at the last
completed region (`src/cli/Session.cpp:83-92`; the short-circuit at
`HybridRecovery.cpp:143` keeps the failed region out of it), so a re-run resumes
(`Session.cpp:69-75`) — and nothing tells the operator any of that. Worse: a device
lost during the *filesystem* pass is read as "no filesystem" in hybrid mode
(`HybridRecovery.cpp:71-76`), so the run answers a dead disk by scheduling a
whole-device carve of it.

**Destination full.** The "free-space check up front" promised in
`recovery-output.md:58-59` does not exist — nothing in the tree calls
`std::filesystem::space`. A full disk is met one artifact at a time: an output file
that will not open is `kIoFailure` with offset 0
(`src/recovery/ExtractFile.cpp:148-151`); a write that dies at flush carries offset =
bytes written *to the output file* (`:113-119`); and `RecoverySink::record` pushes
whichever offset it got into the manifest's `unreadable` list
(`src/recovery/RecoverySink.cpp:121-126`) — the list story-0604 expects to hold device
offsets. The loop then grinds through every remaining winner against the same full disk
(`:145-150`). The ending depends on where the session lives: at the default
`<destination>/.revenant` (`src/cli/RecoveryOptions.cpp:174-179`) the manifest's own
write fails too (`src/recovery/Manifest.cpp:142-152` →
`src/cli/RunDelivery.cpp:84-93`) and the run exits 1 leaving files nothing accounts
for; with `--session` on another volume the manifest writes, `scanComplete` is true,
and a run that delivered none of its winners **exits 0** (`Frontend.cpp:55`).

**Session unwritable.** Caught up front only in the crudest case: a session path
occupied by a regular file fails `create_directories`
(`src/cli/RecoveryRun.cpp:113-123`), an unwritable directory fails
`CandidateIndex::create` (`CandidateIndex.cpp:94-103`) — both behind the same generic
line, exit 1. Mid-run, checkpoint failures are counted into `Checkpointer::unwritten_`
(`Session.cpp:83-92`) and `unwritten()` has no caller anywhere in the tree, so a run
whose "interrupting is safe" promise has quietly lapsed keeps running as if it held.
Lost index appends are counted (`src/recovery/IndexingVisitors.cpp:44-47`) and then —
after the whole scan has been paid for — converted to one contextless `kIoFailure`
(`RecoveryRun.cpp:171-177`).

**The fixture is positional, the failure is temporal.** `FaultyDevice` models a fault
as a byte range, permanent or clearing after N refusals
(`tests/support/FaultyDevice.hpp:15-23`, `FaultyDevice.cpp:76-84`); reads *before* the
range keep succeeding after it fires. A reset enclosure does not work that way: after
the moment of death, every read fails, whatever its offset.

**What story-0604 changed under this story, checked before building on it.**
Three of the measurements above were taken before 0604 landed and no longer
hold, and one of them retires an acceptance criterion outright.

- `RecoverySink::record` no longer pushes a write failure's offset into
  `unreadable`; `Extraction::unreadable` does not exist. The manifest's
  `unreadable` is the source stack's `BadRange` set, device-absolute, and
  nothing else can reach it. **The criterion "the manifest's `unreadable` list
  contains device offsets only" is therefore already met**, and this story keeps
  it only as a regression to not undo.
- `openSource` returns a `SourceStack`, so the give-up policy has an owner that
  already exists: the stack is the one object holding both the device and what
  it refused.
- A destination write failure is already visible as an artifact with outcome
  `failed` and a `failed` count, so what this story adds there is the *type* and
  the stop, not the record.

**And the trap this story exists to close:** once story-0604 wires `RetryingDevice` in,
`readAt` can no longer fail mid-device at all — `readSectorwise` always advances,
zero-filling each sector that stays bad (`src/core/io/RetryingDevice.cpp:57-79`). The
decorator that makes one bad sector survivable makes a *vanished device* invisible: the
run would transcribe the corpse as zeros, sector by retried sector, to the end of the
disk. Today one bad read kills the run rudely; after 0604 nothing kills it at all.
Both ends are wrong. This story defines the middle.

## Design decisions

**After story-0604, on the composed stack.** Two reasons, either sufficient. Without
0604 there is no channel by which "what was lost and where" reaches the manifest, and
building a private one here would be replaced by 0604 immediately. And 0604 *creates*
this story's central question — when does persistence become denial? The answer is a
give-up policy: a contiguous run of sectors still unreadable after retries, longer than
a stated bound, is not a bad patch but a lost source, and the stack reports it upward
as a typed error instead of absorbing it. The bound is a named constant with a stated
rationale, like every other number this project commits to.

**The taxonomy is typed at the error, not guessed at the frontend.** Two new
`ErrorCode` values (`Error.hpp` says extend when a story needs it; this one does):
source lost, and storage exhausted — the latter raised where ENOSPC-class OS errors
surface at the sink and the session seams. The frontend can only map what the layers
below distinguish.

**A `RunOutcome` enum replaces the bool chain.** `runFrontend` returns it; both mains
map it to documented exit codes:

| Code | Outcome | Meaning |
|:---:|---|---|
| 0 | finished | scan complete, manifest written; per-artifact failures are recorded, not hidden |
| 1 | could not start | nothing was produced (source or destination refused) |
| 2 | usage error | the grammar refused the arguments |
| 3 | stopped early, resumable as-is | interrupt or source lost; re-running the same command continues |
| 4 | stopped early, operator action first | destination full or session unwritable; the last words say which and what to do |

The code answers "what should the caller do next"; stderr and the manifest answer
"what happened". A code per cause was considered and rejected: it would freeze a new
integer for every future way to die, and scripts branch on what-next, not on which
component. Exit codes freeze at 1.0 ([versioning.md](../../versioning.md)), so the
table is documented in `README.md`'s CLI section and both `--help` texts now, while it
can still be wrong cheaply.

**Nothing is ever rolled back.** Files extracted before the stop stay, whole and
hashed — a short write is already a failure, never a truncated file. The index and
checkpoint stay; resumption is story-0117's machinery, connected, not duplicated. What
changes is only that a stopped run *says* all of this.

**The manifest is written on every path that got past opening — including a full
destination.** The mechanism this story first committed to was a sized reservation:
preallocate a file after arbitration, big enough for the final manifest, and write into
it on the way out. That does not work, and the reason is worth recording rather than
discovering twice. The bound it needs is not computable — the manifest's length depends
on the `invented` ranges each artifact carries, and those are discovered *during*
extraction, by the reads extraction makes. A reservation whose bound can be exceeded is
a reservation that fails exactly when it is needed.

What is committed instead delivers the same guarantee with no bound at all: **write the
manifest once before extraction begins, and replace it by rename afterwards.**
Extraction is what fills a destination, so the first write happens while there is still
room; it records the run as `in-progress`. The final manifest is assembled beside it and
renamed over it — the same replace pattern the checkpoint uses
(`Checkpoint.cpp:84-122`) — so a replacement that runs out of room never happens and the
earlier manifest stands. A stale manifest saying the run was still going is a far better
record than recovered files nothing accounts for, which is what the old code left.

**The manifest records the loss in fields, not prose.** Alongside today's
`source`/`destination`/`mode`/`winners`/`suppressed`/`unreadable`/`artifacts`
(`Manifest.cpp:125-134`): an `outcome` member mirroring `RunOutcome`, `scannedUpTo` —
the cursor the scan actually reached — and, on a lost source, the offset the device
stopped answering at. `unreadable` returns to meaning what its name says — device
offsets of failed source reads. Destination write failures stop being smuggled into it
(the fix to `RecoverySink.cpp:124`); they are already visible as artifacts with outcome
`failed`.

**Each failure class stops the run instead of decorating it.** Storage exhausted ends
the extraction loop at the first typed failure — every further write against a full
disk is known futile. A session that stops taking writes mid-run — checkpoint or index —
is a broken resume promise and stops the run as session-unwritable; `unwritten()`
finally gets its consumer or the counter goes. And the hybrid mount fallback learns the
difference between "no filesystem" and "no device": a source-lost error during the walk
stops the run rather than launching a whole-device carve of a dead disk
(`HybridRecovery.cpp:71-76`).

**The last words follow the M4 standard.** Each stop states what happened, what
survived, and the exact next step — "re-run the same command to continue from byte N",
"free space or point --destination elsewhere, then re-run" — in `describe`/`summarize`
(`RunSummary.cpp`), where the words already live.

**The give-up bound can be wrong, and what that costs is worse than a stop.**
`kLostSourceRunBytes` is a choice rather than a measurement. A megabyte is far
more than a hard disk's reallocation run, but a flash erase block reaches
several megabytes and a scratched band on a platter can too — and a defect that
large is read as a lost device. The cost of that misfire is not a stopped run
but a *stuck* one: the checkpoint advances only at a completed scan region, so
the region holding the defect never completes, the resume point never passes it,
and every re-run reads the same defect and stops in the same place while the
last line on stderr says "re-run the same command to carry on". Before this
story that disk was recovered with the defect zero-filled.

Nothing here fixes that, and pretending otherwise would be worse than saying it.
What it needs is a way for an operator to say "this really is a defect, read
through it" — a flag on the bound, or a resume point that can step past a region
it could not finish. Either is a story; this one records the limit rather than
leaving it to be discovered.

**The fixture grows a device-loss mode.** A latch, not a range: from the first refused
read, every subsequent read fails whatever its offset — which is what a reset enclosure
does. It lives in `tests/support/FaultyDevice` beside the modes story-0402 built.

## Acceptance criteria

- [x] `runFrontend` returns `RunOutcome`; both mains map it to exit codes 0/1/2/3/4;
      the table is documented in `README.md` and both `--help` texts.
- [x] A source lost mid-scan ends the run promptly (bounded give-up, no zero-transcription
      of the remainder), exits 3, writes the manifest with `outcome` and `scannedUpTo`,
      and leaves a checkpoint the next run resumes from.
- [x] A source lost mid-extraction keeps every artifact already written, records the
      remaining winners as not attempted, records the loss offset, and exits 3.
- [x] A full destination stops extraction at the first storage-exhausted failure, keeps
      what was written, still produces `manifest.json` via the reservation, and exits 4.
- [x] An unwritable session directory is refused up front; a session lost mid-run
      stops the run and exits 4 (up-front refusal of a run that produced nothing
      exits 1). **The message does not name the path**, and cannot as `Error` is
      typed: it carries `{code, offset, osCode}` and `describe` takes only the
      code, so a sentence naming a path would need the error to carry one. That
      is a change to the error type every layer shares, which is a story of its
      own rather than a clause of this one.
- [x] The manifest's `unreadable` list contains device offsets only —
      already true after story-0604, kept here as a regression to not undo.
- [x] An interrupted (`Ctrl-C`) run exits 3, not 1.
- [x] Every stop's stderr states what happened, what survived, and the next step.

## Test plan

Integration (`tests/integration/StoppedRunTest.cpp`, in-process over the composed
stack — the CLI binaries cannot be handed a `FaultyDevice`): device-loss latch fires
mid-scan → typed outcome, checkpoint at the last completed region, index intact,
manifest content as specified; the same run re-driven against the healed device
recovers exactly what an uninterrupted run does. Latch fires mid-extraction →
already-written artifacts survive and hash, manifest records the loss and the
unattempted winners.

Unit: `tests/unit/cli/` — the error-to-`RunOutcome`-to-exit-code mapping, one case per
row of the table; `RunSummaryTest.cpp` — the wording of each stop.
`tests/unit/recovery/RecoverySinkTest.cpp` — a storage-exhausted write is typed, stops
the loop, and never enters `unreadable`; `ManifestTest.cpp` — the new fields serialize.
`tests/support` — the loss-latch semantics of the grown `FaultyDevice`.

On the WSL workbench (story-0603's mold): a small loop-device filesystem filled
mid-extraction proves the manifest reservation survives a real ENOSPC, and a
`chmod 500` session directory proves the mid-run session-loss path; recorded in this
story on completion, since CI has neither loop devices nor permission to drop them.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] No performance regression. The first CI run failed the benchmark gate on
      instruction count — `ntfs-enumerate` +27%, `carve-validate` +6.3% — and the
      cause was this story's own DRY refactor: sharing the checkpoint's
      replace-by-rename made the *manifest* write one `put()` per byte, and a
      manifest grows with the winner set. It writes in one call now. Measured on
      the workbench against `main`: `ntfs-enumerate` 555,771,959 -> 555,804,414
      instructions (+0.006%) and `carve-validate` 222,187,369 -> 222,226,510
      (+0.02%), both far inside the 5% threshold.
- [x] clang-format, clang-tidy, duplication and file-length guard clean on both
      platforms.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md))
      completed. It earned its keep: a source lost during *extraction* was being
      absorbed into a per-artifact failure and the run exited 0; `scannedUpTo`
      was always zero on the very path the story exists for; `stoppedAt` accepted
      any error's `offset`, whatever coordinate system it was in, which is the
      mixing story-0604 had just removed from `unreadable`; the `did-not-start`
      fallback the comment called unreachable was live; and a debug `std::cerr`
      had been left in a committed test. It also caught that the bound's stated
      rationale was false, which is why the limit is now written down above.
