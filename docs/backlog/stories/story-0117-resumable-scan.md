<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0117: Resumable scan — checkpoint, resume, clean cancellation

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: L

## Goal

Make an interrupted recovery cost the unscanned tail rather than the whole
device. Scanning a multi-terabyte disk takes hours, it usually runs against
hardware that is already failing, and
[ADR-0008](../../architecture/adr/adr-0008-resumability-checkpointing.md) is blunt
about why starting over is not acceptable: *"re-reading a dying disk can hasten
its death."*

[story-0112](story-0112-candidate-index-arbitration.md) built the index for this
— append-only, blob-before-record, validated on reload. This story adds the two
things it was waiting for: a durable cursor saying how far the scan got, and the
run that picks up from it.

## Design references

- [ADR-0008](../../architecture/adr/adr-0008-resumability-checkpointing.md) — a
  durable session directory holding the index, the scan progress, and the
  manifest; checkpoints at bounded intervals; a clean flush on cancellation; and
  session state that is validated on reload rather than trusted.
- [ADR-0006](../../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md)
  — discovery is separate from extraction, which is what makes "scan now, decide
  later" possible at all.
- [ADR-0009](../../architecture/adr/adr-0009-output-safety.md) — a checkpoint read
  back is untrusted data like any other.

## Scope

1. **The checkpoint** — `recovery::Checkpoint`: a fixed 64-byte record in the
   session directory holding the run's shape, the scan cursor, and how many
   candidates the index held when it was written. Replaced by write-then-rename,
   so a crash leaves either the old checkpoint or the new one.
2. **Bounded progress** — `recovery::ScanProgress`: the seam the orchestrator
   reports through after every bounded chunk of device, and answers back through.
   One method, two jobs: checkpointing, and stopping.
3. **Resuming** — `RecoveryPlan::resumeFrom`, `CandidateIndex::reopen`, and the
   region clipping that turns a cursor into "only the unscanned remainder".
4. **Clean cancellation** — `cli::catchInterrupts`: `Ctrl-C` finishes the chunk
   it is on, checkpoints, and stops.
5. **The session** — the frontend's decision to resume or start fresh, and what
   an incomplete run reports.

## Design decisions

**An interrupted run does not extract.** This is the decision the whole story
turns on. Arbitration over a partial index can crown a winner the finished scan
would have suppressed — the candidate that would have beaten it is in the tail
nobody has read yet — so extracting from it would write files a complete run
never would. An interrupted run therefore checkpoints, reports that the scan is
incomplete, and stops: no arbitration, no extraction, no manifest. What it leaves
behind is exactly what the next run needs.

**The exit status says the run is unfinished; the summary says why.** The
operator sees the full three-line report *and* a non-zero exit, so
`revenant-undelete && …` does not run the next step on a half-finished recovery.

**A checkpoint identifies its run by shape, not by field.** Mode, source size and
format allowlist are hashed into one SHA-256 (story-0115's, already in core), and
the checkpoint stores only the digest. A session belonging to a different
recovery is then rejected wholesale rather than half-matched — and the checkpoint
stays a fixed 64 bytes, which is what lets it be replaced by a single small
write-and-rename.

**Resuming truncates the index back to the checkpoint.** The tail an interrupted
run appended after its last checkpoint describes a region the resumed scan is
about to read again. Dropping it is what keeps the index and the cursor saying
the same thing; keeping it would double every candidate in that window and inflate
the suppression count with candidates that never lost to anything. The blob keeps
the bytes those dropped records pointed at — they are unreferenced, bounded by one
checkpoint interval, and reclaiming them would mean rewriting an append-only file.

**A resumed run re-walks the filesystem for its accounting alone.** The carve
pass searches the gaps between confidently recovered files, so it cannot know
where to look without the byte accounting — and the accounting lives in the
filesystem pass. Re-walking it costs a metadata pass, not a device pass, so a
resumed run pays milliseconds rather than hours. Its entries are already in the
index, so they are not appended a second time.

**Cutting a scan region in two loses nothing.** A region bounds where a signature
is *looked for*, never how long a file may be, and the scanner already reads a
signature's reach past a region's end so a magic straddling the boundary is still
found. Cutting the gaps into `checkpointBytes` chunks is therefore free, and it
is what makes the checkpoint interval bounded on a device with one enormous gap —
which is exactly what carve-only mode over a formatted disk produces.

**`Ctrl-C` means "stop cleanly", not "abort".** The handler sets a flag; the run
finishes the chunk it is on and checkpoints. A second `Ctrl-C` falls through to
the default handler, so an operator who really means it is never trapped.

**Extraction resumability is not in this story.** ADR-0008 lists it as a
consequence, and it needs something this story does not build: a per-artifact
record of what has been *committed*, because a file half-written by a crash is
indistinguishable on disk from a complete one. Since an interrupted run does not
extract at all, nothing here is left half-done — a resumed run that completes its
scan extracts once, from a complete index.

## Acceptance criteria

### `Checkpoint`

- [x] `writeCheckpoint` puts a fixed-size record in the session directory, and
      `readCheckpoint` returns exactly what was written.
- [x] Replacing a checkpoint leaves either the old one or the new one, never a
      half-written one.
- [x] A missing checkpoint is `kNotFound`; a foreign magic, a version mismatch,
      or a short file is `kInvalidArgument` — never a guess.
- [x] Every stored integer is explicitly little-endian.

### Resuming

- [x] `CandidateIndex::reopen(directory, records)` keeps the first `records`
      candidates, drops the rest, and appends after them.
- [x] `reopen` on an index holding fewer records than asked for is
      `kInvalidArgument`.
- [x] A plan with a resume cursor scans only what lies at or after it.
- [x] A resumed run does not report filesystem entries a second time.
- [x] Regions are cut to `checkpointBytes`, so progress is reported at bounded
      intervals however few gaps a device has.

### Cancellation

- [x] A `ScanProgress` that answers "stop" ends the scan after the chunk it is
      on, and the run reports the scan as incomplete.
- [x] `catchInterrupts` makes `interrupted()` true after a `SIGINT`.

### The frontend

- [x] A session directory holding a compatible checkpoint is resumed; one holding
      an incompatible, corrupt or absent checkpoint starts fresh.
- [x] An interrupted run writes no manifest and extracts nothing.
- [x] An interrupted run exits non-zero and says the scan is incomplete.
- [x] A resumed run that finishes recovers exactly what an uninterrupted run
      would.

## Test plan

Unit (`tests/unit/recovery/CheckpointTest.cpp`): a round trip; a missing file; a
bad magic; a bad version; a truncated file; a replacement over an existing one.

Unit (`tests/unit/recovery/CandidateIndexTest.cpp`): reopening keeps the asked-for
records and drops the tail; appending after a reopen continues the ordinals;
asking for more records than the index holds is refused.

Unit (`tests/unit/recovery/HybridRecoveryTest.cpp`): a resume cursor skips what is
behind it; a progress reporter that says stop ends the scan and the stats say
incomplete; regions are chunked to the plan's checkpoint size; progress is
reported at every chunk boundary.

Unit (`tests/unit/cli/InterruptTest.cpp`): a raised `SIGINT` is observed.

Integration (`tests/integration/ResumedRecoveryTest.cpp`): a run over the
story-0118 fixture stopped after its first chunk leaves a checkpoint and no
manifest; re-running it finishes the scan and recovers exactly the files an
uninterrupted run does, byte for byte.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Coverage held or raised (>= 85% core) — enforced by the CI coverage job;
      the instrumentation is GCC/Clang-only, so it is not reproducible on the
      local MSVC build.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `README.md` updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
