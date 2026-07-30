<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0606: Soak and a long fuzz campaign — the tests CI could never afford

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: Ready
- Size: M

## Goal

CI gives every byte parser twenty seconds of fuzzing and every recovery run a few
hundred mebibytes of device. This story buys, once, what that budget never could: hours
of libFuzzer per parser with every finding triaged to a fix and a corpus entry, and a
soak run over hundreds of gigabytes that turns "streaming, always"
([strategy.md](../../performance/strategy.md)) from a principle into a measurement —
memory flat across the whole run, and an interrupt at an arbitrary point resumed to the
same result. The hours are spent once; what they leave behind — an enlarged corpus, a
unit test per crash, a written memory bound — keeps paying on every CI run after.

## Design references

- [`.claude/skills/fuzz-campaign/SKILL.md`](../../../.claude/skills/fuzz-campaign/SKILL.md)
  — the campaign loop this story executes: background runs, triage oldest-first,
  deduplicate by stack top, minimize, fix test-first, promote with `-merge=1`. Also the
  platform ruling: Windows-recipe crashes are distrusted until they reproduce under the
  `debug` preset or on Linux.
- [`.claude/skills/wsl-bench/SKILL.md`](../../../.claude/skills/wsl-bench/SKILL.md) —
  the bench the hours run on: Debian 13 under WSL2, 8 cores, 15 GB RAM, ~956 GB free.
  The only local environment where libFuzzer links at all.
- [`ci.yml`](../../../.github/workflows/ci.yml) — the `fuzz-smoke` job: every target,
  twenty seconds each, `-rss_limit_mb=512`, over its committed corpus. This is the gate
  that will regress everything the campaign finds, forever.
- [`CMakePresets.json`](../../../CMakePresets.json) — the `fuzz` preset (clang++,
  `REVENANT_BUILD_FUZZERS=ON`) the bench builds with.
- [benchmarks.md](../../performance/benchmarks.md) — how peak RSS is measured (from
  outside the process, the OS-reported high-water mark,
  [`tools/perf/peakmemory.py`](../../../tools/perf/peakmemory.py)), and the **10%**
  peak-RSS threshold this story borrows as its flatness tolerance.
- [story-0117](story-0117-resumable-scan.md) /
  [ADR-0008](../../architecture/adr/adr-0008-resumability-checkpointing.md) — the
  checkpoint, `resumeFrom`, and clean-`SIGINT` machinery the soak's interrupt exercises
  at the scale it was built for.
- [story-0115](story-0115-session-manifest.md) /
  [recovery-output.md](../../architecture/recovery-output.md) — the manifest: source
  extents `(offset, length)` and a SHA-256 per recovered file. The instrument the
  resume-equivalence comparison reads.
- [ADR-0003](../../architecture/adr/adr-0003-validating-carving.md) — the founding
  claim the fuzzers interrogate: parsers that face hostile bytes and never run away.

## What was measured

Counted on 2026-07-30, at the current tree:

- **28 fuzz targets** in `tests/fuzz/*Fuzz.cpp`, one per byte parser: six format
  carvers (JPEG, PNG, MP4, ZIP, PDF, raw), four NTFS parsers (boot sector, MFT record,
  runlist, enumeration), three FAT32, two exFAT, six ext4 (superblock, inode, extent
  tree, directory entry, journal, enumeration), MBR and GPT, and five pieces of shared
  machinery (`ByteReader`, name decoding, output paths, the signature scanner,
  SHA-256).
- **164 committed corpus inputs across 28 directories, distributed as debt**:
  `NtfsEnumerateFuzz` holds 114, `MftRecordFuzz` 18, `RunlistFuzz` 11 — and **11
  directories hold nothing but a `.gitkeep`**, among them all six format carvers. Every
  carver fuzzes from nothing, twenty seconds at a time, rediscovering the same shallow
  prefixes on every CI run.
- **The generator cannot build the soak fixture today.** `revenant-imagegen pattern`
  streams one sector at a time
  ([`PatternWriter.cpp`](../../../tools/imagegen/PatternWriter.cpp)), so its 64-bit
  size argument makes a 256 GiB image a wait rather than a limit — but it plants
  nothing carveable. `carve` plants files, but
  [`CarveCorpus.cpp`](../../../tools/imagegen/CarveCorpus.cpp) builds the entire image
  in memory (`corpus.reserve(sizeBytes)`) before writing a byte, so at soak scale it
  is itself the unbounded allocation this story exists to disprove. The soak needs a
  verb neither is: streamed, with content.

## Design decisions

**A recorded one-off, not a gate.** The 15-minute CI budget is a decision this story
respects, not one it overturns. The campaign's deliverables are (a) the per-target
table recorded in this story — CPU-hours, executions, coverage at close, findings and
their disposition; (b) every real fault fixed in its own conventional commit
referencing this story, with a unit test grown from the minimized reproducer; and
(c) the minimized corpus merged and committed, so the existing twenty-second
`fuzz-smoke` pass replays every finding on every push from now on. The hours run once;
the regression protection is permanent and costs CI nothing it is not already paying.

**Everything long runs on the WSL bench.** The Windows `fuzz` preset never links, and
the Windows fallback recipe fabricates `container-overflow` reports — both documented
in the [fuzz-campaign](../../../.claude/skills/fuzz-campaign/SKILL.md) skill, which
also rules that no Windows-recipe crash is believed until it reproduces on Linux or
under the `debug` preset. The soak fixture lives on the bench's own filesystem (the
~956 GB free the [workbench doc](../../../.claude/skills/wsl-bench/SKILL.md) records),
not across the `/mnt/d` boundary.

**How long "hours" is: at least 4 CPU-hours per target, or until coverage plateaus,
whichever is later.** Four CPU-hours is the skill's own campaign figure
(`-max_total_time=14400`) and 720 times the CI allowance; a target still finding new
coverage at the four-hour mark (read from `-print_final_stats`) keeps running until it
stops. Twenty-eight targets at four CPU-hours is 112 CPU-hours, which the bench's
eight cores finish in a background weekend — the point of a floor stated in CPU-hours
is that nobody sits watching it.

**The empty corpora are seeded before the clock starts.** Eleven targets currently
start from a `.gitkeep`; a campaign begun there spends its first hours learning what
the unit-test fixtures already know. Each empty corpus is seeded from the checked-in
byte fixtures for its parser, and `-merge=1` afterwards keeps only inputs that earn
their place.

**The soak bound is stated numerically, up front.** Memory must be a function of
configuration, not of device size: peak RSS over a **≥ 256 GiB** run stays within
**10%** — the perf gate's own cross-machine tolerance
([benchmarks.md](../../performance/benchmarks.md)) — of the same binary's peak over
the M5 suite's 128 MiB scan fixture ([`cases.py`](../../../tools/perf/cases.py)) on
the same bench. That is a 2,048× increase in
input against a single-digit-percent allowance on memory; a working set that grows
with the device fails it immediately. The headline peak is taken exactly as the
harness takes it — the OS-reported high-water mark, from outside the process
([`peakmemory.py`](../../../tools/perf/peakmemory.py)) — and an RSS series sampled at
a fixed interval from `/proc` across the run shows *flat*, not merely
peak-bounded-at-the-end.

**256 GiB is chosen the way M5 chose 48.** The fixture must dwarf the bench's 15 GB
of RAM so the page cache cannot quietly turn a streaming claim into a caching one —
the same reasoning [strategy.md](../../performance/strategy.md) records for scanning
48 GiB against 31.5 GiB of RAM. Seventeen times RAM, and comfortably inside the
bench's free space, twice over for the extraction output.

**The soak fixture gets the streamed verb imagegen is missing.** A new generator verb
writes pattern filler sector-by-sector (the `pattern` path already does this) with a
bounded, known set of carveable files planted at recorded offsets. Bounded, because
extraction must be bounded for the manifest comparison to be cheap; known, because
planted offsets are ground truth the manifest can be checked against, not just checked
for self-consistency.

**Resume-equivalence is a manifest diff.** The run is interrupted with `SIGINT` at an
arbitrary point — deliberately not a checkpoint boundary — and resumed to completion;
a second, uninterrupted run over the same fixture is the control. Equivalent means:
the recovered-file entries — path, source extents `(offset, length)`, size, SHA-256
([story-0115](story-0115-session-manifest.md)) — are identical, with run metadata
(timestamps, durations) excluded by the comparison script. The hashes make this a
byte-for-byte claim without keeping two output trees.
[story-0117](story-0117-resumable-scan.md) already proved it over a fixture measured
in mebibytes; this story repeats the assertion where a re-scan costs hours and the
checkpoint is load-bearing rather than decorative.

## Acceptance criteria

- [ ] The campaign table is recorded in this story: one row per target — all 28 —
      with CPU-hours run, total executions, coverage at close, and every finding's
      disposition.
- [ ] Zero open findings at close: every artifact directory is empty, and every real
      fault has its own conventional-commit fix referencing this story plus a unit
      test grown from the minimized reproducer. An OOM or timeout is a finding, not
      noise.
- [ ] The merged, minimized corpora are committed for all 28 targets; no corpus
      directory holds only a `.gitkeep`; the `fuzz-smoke` job is green over the
      enlarged corpus.
- [ ] The soak ran over a ≥ 256 GiB generated image on the WSL bench; its log — the
      RSS series and the OS-reported peak — is recorded in this story, and the peak
      is within 10% of the same binary's peak over the 128 MiB perf fixture on the
      same bench.
- [ ] The soak run was interrupted at an arbitrary point, resumed, and completed; the
      manifest comparison against an uninterrupted control run shows identical
      recovered-file entries, and the comparison method is recorded with the result.
- [ ] The new imagegen verb exists, streams (no allocation proportional to image
      size), documents itself in the usage line, and is unit-tested.

## Test plan

- Unit (`tests/unit/`, per triaged crash): each minimized reproducer becomes a
  checked-in byte fixture asserting the parser's verdict — the fuzzer found it once;
  the unit test keeps it found even if the corpus were lost.
- Unit (imagegen): the new verb is deterministic, plants exactly the files it was
  asked at the offsets it records, and produces byte-identical output for the same
  arguments — the same bar the existing builders meet.
- Automated afterwards: CI's `fuzz-smoke` replays the merged corpora — every campaign
  finding, regressed in twenty seconds per target on every push — and
  `tests/integration/ResumedRecoveryTest.cpp` keeps asserting resume-equivalence at
  small scale.
- Not automated: the hours and the gigabytes themselves. The 112+ CPU-hours and the
  256 GiB run are recorded in this story on completion, the way
  [story-0607](story-0607-format-gate-argument-list.md) records its platform
  verification — a gate cannot afford them, which is the premise of the story.

## Definition of Done

- [ ] Acceptance criteria met, tests green under ASan + UBSan.
- [ ] clang-format, clang-tidy, duplication and file-length guard clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
