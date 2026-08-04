<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0606: Soak and a long fuzz campaign — the tests CI could never afford

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: In progress
- Size: M

## Goal

CI gives every byte parser twenty seconds of fuzzing and every recovery run a few
hundred mebibytes of device. This story buys, once, what that budget never could: a
libFuzzer campaign per parser measured in CPU-hours rather than seconds, with every
finding triaged to a fix and a corpus entry, and a soak run over hundreds of gigabytes
that turns "streaming, always" ([strategy.md](../../performance/strategy.md)) from a
principle into a measurement — memory flat across the whole run, and an interrupt at an
arbitrary point resumed to the same result. The hours are spent once; what they leave
behind — an enlarged corpus, a unit test per crash, a written memory bound — keeps
paying on every CI run after.

**What the story turned out to be about.** The campaign found, before it started, that
the fuzzers had no coverage feedback from the code under test: the library every target
links carried no instrumentation, so hours and seconds bought the same nearly-random
search. Fixing that is worth more than any amount of the time this story was arguing
about, and it is the reason the campaign was allowed to be 14.5 CPU-hours rather than
the 112 originally scoped — see the design decision below, which states what the
smaller number gives up rather than pretending it gives up nothing.

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

Counted on 2026-07-30 and **re-counted on 2026-08-03 before the work began**, because
two of the figures had gone stale in the four days between:

- **29 fuzz targets** in `tests/fuzz/*Fuzz.cpp`, one per byte parser: six format
  carvers (JPEG, PNG, MP4, ZIP, PDF, raw), four NTFS parsers (boot sector, MFT record,
  runlist, enumeration), three FAT32, two exFAT, six ext4 (superblock, inode, extent
  tree, directory entry, journal, enumeration), MBR and GPT, and six pieces of shared
  machinery (`ByteReader`, name decoding, output paths, the signature scanner,
  SHA-256, and the mount table). The original count of 28 predates
  `MountTableFuzz`, which arrived with story-0609 four days later.
- **31 committed corpus inputs across 29 directories** — and **11 directories hold
  nothing at all**, among them all six format carvers. Every carver fuzzed from
  nothing, twenty seconds at a time, rediscovering the same shallow prefixes on
  every CI run.

  The story's original figure was 164, and this story's own fact-check first
  "confirmed" 168. Both are wrong, and wrong the same way: they count the working
  tree, where `find` sees whatever previous local fuzz runs left behind.
  `.gitignore` tracks only curated `*.bin` seeds, so `git ls-files` is the
  instrument the word *committed* asks for, and it answers 31. `NtfsEnumerateFuzz`
  did not hold 114 inputs; it held one, plus 113 untracked leftovers. Checking a
  claim with the wrong instrument reproduces it rather than testing it.
- **The fuzzers cannot see the code they fuzz.** Found on arrival, before a single
  campaign hour was spent, and the most important thing in this story.
  `revenant_add_fuzz_target` puts `-fsanitize=fuzzer` on the *target*, which
  instruments that one translation unit; `librevenant`, which holds every parser,
  was compiled with no SanitizerCoverage at all. libFuzzer's coverage feedback
  therefore came from the harness file alone — thirteen counters for
  `JpegCarverFuzz`, forty-five for `NtfsEnumerateFuzz` — so every campaign this
  project has run, and the twenty-second `fuzz-smoke` on every push, has been
  near-random input generation wearing a coverage-guided fuzzer's name. Nothing
  about it was visible from outside: the targets built, ran, and exited zero.
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

**How long "hours" is: 30 CPU-minutes per target, not the four CPU-hours this
story first asked for.** The original figure — 4 CPU-hours × 28 targets = 112
CPU-hours, "a background weekend" — was scoped down by the maintainer to a
campaign proportionate to the rest of the milestone, and this is the honest
record of what that buys and what it gives up.

What it buys: 29 targets × 30 minutes = **14.5 CPU-hours**, ninety times the CI
allowance per target, over a corpus that is seeded rather than empty and — for
the first time in this project — with the library instrumented, so the hours are
coverage-guided rather than random. Against the campaign this story originally
specified, the instrumentation fix is worth more than the other 97 CPU-hours
would have been: 112 hours of blind mutation explores less than 14 guided ones.

What it gives up, stated rather than glossed: the deep tail. Bugs behind a
multi-stage input — a valid container whose third nested structure is malformed —
are found by coverage plateaus measured in hours, not half-hours. **This story
therefore does not claim the parsers have been exhaustively fuzzed.** It claims
they have been fuzzed with feedback, from a seeded corpus, for the first time,
and that everything found was fixed. The remaining depth is a known limit, and
belongs on the 1.0 limits page in [epic-m7](../epic-m7-release.md#notes) or in a
follow-up campaign story — not in a criterion this story quietly rewrote to match
what it did.

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

**The plan is a file beside the image, and every plant is a different file.**
Ground truth has to survive the run that consumes it, so the offsets are written
to `<image>.plan` rather than printed to a stream somebody has to remember to
redirect. And the plants are stamped with their own offsets: the extractor
deduplicates by content hash, so 256 copies of one JPEG recover as one file and
255 duplicates, and every artifact in the manifest carries the same SHA-256 —
a comparison that cannot see a hash attached to the wrong file. Found by running
the pipeline over a four-plant fixture before committing to a 256 GiB one.

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

## The campaign, as it ran

29 targets × 30 CPU-minutes, seven at a time on the bench, 2026-08-03, against a
`RelWithDebInfo` build with ASan + UBSan and — for the first time — a library the
fuzzer can see. Every target exited 0. **Zero crashes, zero timeouts, zero OOMs:
the artifact directories are empty**, so there is no reproducer to grow a unit
test from and no fix commit to point at.

`new` is how many of the campaign's inputs `-merge=1` judged worth keeping.

| Target | Executions | cov | ft | new | Artifacts |
|--------|-----------:|----:|---:|----:|----------:|
| `ByteReaderFuzz` | 731,173,117 | 21 | 43 | 15 | 0 |
| `ExfatBootRegionFuzz` | 592,992,790 | 87 | 112 | 29 | 0 |
| `ExfatDirectoryEntryFuzz` | 730,436,364 | 48 | 72 | 21 | 0 |
| `Ext4DirectoryEntryFuzz` | 150,737,141 | 104 | 446 | 136 | 0 |
| `Ext4EnumerateFuzz` | 24,742,139 | 849 | 2,895 | 306 | 0 |
| `Ext4ExtentTreeFuzz` | 288,207,356 | 68 | 236 | 33 | 0 |
| `Ext4InodeFuzz` | 767,064,035 | 28 | 47 | 7 | 0 |
| `Ext4JournalFuzz` | 167,649,716 | 58 | 225 | 37 | 0 |
| `Ext4SuperblockFuzz` | 581,771,506 | 85 | 102 | 16 | 0 |
| `Fat32BootSectorFuzz` | 726,664,104 | 104 | 125 | 27 | 0 |
| `Fat32EnumerateFuzz` | 193,294,420 | 258 | 315 | 40 | 0 |
| `FatDirectoryEntryFuzz` | 302,419,496 | 153 | 340 | 42 | 0 |
| `GptFuzz` | 114,784,212 | 240 | 429 | 56 | 0 |
| `JpegCarverFuzz` | 312,443,897 | 67 | 228 | 89 | 0 |
| `MbrFuzz` | 218,565,423 | 166 | 615 | 67 | 0 |
| `MftRecordFuzz` | 320,195,725 | 242 | 549 | 89 | 0 |
| `MountTableFuzz` | 18,502,058 | 341 | 1,939 | 404 | 0 |
| `Mp4CarverFuzz` | 745,253,169 | 66 | 172 | 58 | 0 |
| `NameDecodeFuzz` | 68,260,279 | 108 | 623 | 233 | 0 |
| `NtfsBootSectorFuzz` | 767,187,663 | 95 | 109 | 24 | 0 |
| `NtfsEnumerateFuzz` | 13,100,726 | 520 | 2,329 | 220 | 0 |
| `OutputPathFuzz` | 32,433,749 | 224 | 946 | 208 | 0 |
| `PdfCarverFuzz` | 615,685,049 | 102 | 200 | 64 | 0 |
| `PngCarverFuzz` | 551,530,147 | 47 | 132 | 36 | 0 |
| `RawCarverFuzz` | 189,649,695 | 155 | 627 | 178 | 0 |
| `RunlistFuzz` | 402,532,453 | 113 | 377 | 107 | 0 |
| `Sha256Fuzz` | 241,055,238 | 51 | 145 | 40 | 0 |
| `SignatureScanFuzz` | 32,076,599 | 282 | 1,334 | 156 | 0 |
| `ZipCarverFuzz` | 1,146,110,892 | 96 | 154 | 48 | 0 |
| **total** | **11,046,519,158** | | | **2,786** | **0** |

**What the numbers say, and what they do not.** Coverage at close is several
times what the same corpora reached at the campaign's start — `NtfsEnumerateFuzz`
520 against 255, `SignatureScanFuzz` 282 against 136 — and both of those start
values are themselves post-fix; before the instrumentation fix the same targets
reported 18 and 22. Zero crashes over eleven billion executions is a real result
for parsers whose founding claim is that they never run away
([ADR-0003](../../architecture/adr/adr-0003-validating-carving.md)). It is *not*
a proof of their absence: half an hour per target does not reach bugs behind a
multi-stage input, which is exactly what the shortened campaign gave up and said
it was giving up.

**The merged corpora are not committed, and the criterion below says so.**
`-merge=1` kept 2,786 of the campaign's inputs, and `fuzz-smoke` replayed all 29
targets green over them — but `.gitignore` tracks only curated `*.bin` seeds and
deliberately ignores libFuzzer's own hash-named finds, "regenerate them with
`tools/fuzz/make_seed_corpus.py`". Committing 2,786 files would reverse that
decision as a side effect of this story, which [AGENTS.md](../../../AGENTS.md) §
"If a rule is wrong, change the rule in a dedicated PR" forbids. With zero
crashes there is also no *finding* to regress: the corpus's only value here is a
coverage head start, and it is worth 28 MB and 2,786 files or it is not — a
maintainer's call, taken as **no**. What this story does leave behind is the
eleven corpora that were empty, seeded and, now, regenerable.

## The soak, as it ran

Run on the Debian 13 WSL2 bench (8 cores, 15 GB RAM) on 2026-08-03, against
`revenant-carve` built from the `RelWithDebInfo` configuration CI publishes —
the same binary for every row. The fixture is `revenant-imagegen soak
soak.img 274877906944 256`: **256 GiB**, 256 planted JPEGs one gibibyte apart,
generated in **286 s** (≈ 920 MiB/s) while the fuzz campaign held seven cores.

| Run | Device | Wall clock | Peak RSS (OS-reported) |
|-----|-------:|-----------:|-----------------------:|
| reference, `--dry-run` | 128 MiB | 0.2 s | **72.1 MiB** |
| reference, extracting | 128 MiB | 0.1 s | 72.0 MiB |
| soak, `--dry-run` | 256 GiB | 408.4 s | **72.1 MiB** |
| soak, extracting (control) | 256 GiB | 428.2 s | 72.2 MiB |

**A 2,048× larger device cost 0.0% more memory** — 72.1 MiB against 72.1 MiB in
the mode [cases.py](../../../tools/perf/cases.py) measures, and 0.3% in the mode
that also writes. The allowance was 10%. The number itself is the configuration
and not the device: 64 MiB of carve bound plus a process. The RSS series, sampled
from `/proc` every 5 s across 82 and 86 samples, reaches its maximum in the first
sample after startup and does not move again — flat, not merely bounded at the end.

Throughput is *not* a measurement here: the campaign was using seven of eight
cores throughout, so 642 MiB/s over the dry-run says nothing about the 1,037 MiB/s
[M5 recorded](epic-m5-performance.md). Only memory was under test.

Both 256 GiB runs found all 256 planted files at the offsets the plan recorded,
scanned all 4,096 regions, reported no unreadable ranges, and the control run
wrote 256 distinct files (8 MiB) with nothing deduplicated.

### The interruption

`SIGINT` at 120 s — 69.3 GiB in, and between checkpoints, since the scan writes
one every 64 MiB and stopped at a cursor no checkpoint names.

| | Candidates | Regions | Outcome | Exit |
|---|---:|---:|---|---:|
| interrupted | 70 | 1,109 | `stopped-resumable`, nothing decided or written | 3 |
| resumed | 186 | 2,987 | `finished`, 256 winners, 256 files written | 0 |
| **sum** | **256** | **4,096** | matches the control run exactly | |

The resumed run rescanned nothing: it read the checkpoint, carried the first 70
candidates across in the index, and scanned only the remaining 2,987 regions —
186.7 GiB of the 256. Peak RSS across the interruption was 72.1 MiB and
72.3 MiB — the same number again.

**The comparison, and the proof it could have failed.**
[`tools/soak/manifest_identity.py`](../../../tools/soak/manifest_identity.py)
compares the two manifests on `originalName`, `writtenName`, `source`,
`confidence`, `outcome`, `bytes`, `sha256`, `extents` and `invented`, ordered so
discovery order cannot matter, with run metadata excluded because `scannedUpTo`
is exactly what legitimately differs. It answered: *256 recovered files identical
across the interruption, covering all 256 planted offsets.*

A green comparator that compares nothing says the same thing, so it was broken
three ways against this run's own data: one artifact's SHA-256 changed (`FAIL
artifact 86 sha256`), one artifact dropped (`FAIL artifact count: control 256,
resumed 255`), and one extent moved off its plant (`FAIL 1 planted files were not
recovered, first at 9663676416`). Its unit tests cover the two vacuous passes —
two empty manifests are identical, and a plan with no plants proves nothing.

## Two defects this story wrote, and the test that found them

Worth recording because both are the same shape as the bug the soak exists to
disprove: a number reported without being measured.

Folding three copies of "open an image, write it, map a bad stream to an error"
into one helper made `writeImageBytes` report `.offset = image.size()` on a
failure — *we wrote all of it* — where before it reported nothing.
[`Error`](../../../include/revenant/core/Error.hpp)'s contract is that `offset`
is meaningful or absent, and "all of it" is neither. `writeBytesTo` now returns
what the stream actually took, because an output iterator keeps going after the
stream goes bad.

Writing the test for that found a second one, older: `writeChunk` advanced its
offset whether or not the sector was written, so `writeFiller` reported a
failure one sector past the last byte the stream took. Both are now pinned by
`PatternWriter.FillerReportsTheOffsetItReachedWhenTheStreamFails` and
`WriteBytesToReportsWhatTheStreamTook`, which drive a `streambuf` that refuses
everything after N bytes — the only way a unit test can reach a full disk.

## What did not survive contact with the story

Four claims this story or its neighbours rested on turned out to be wrong when
checked against a real system, which is the point of checking before building.

- **"28 fuzz targets, 164 corpus inputs."** Both were true on 2026-07-30 and
  stale by 2026-08-03: `MountTableFuzz` and its four inputs arrived with
  story-0609 in between. A count written into a story is a fact with an expiry
  date, and this one expired in four days.
- **`kCounter` does not encode an absolute offset.** Its comment said "offsets
  self-describe", and `PatternWriter`'s own test is named
  `CounterPatternEncodesAbsoluteOffset` — but byte *j* of sector *n* is
  `(n*512 + j) & 0xFF`, and `n*512` is always a multiple of 256. Every sector
  holds the same 512 bytes; an offset self-describes modulo 256 and no further.
  A soak test asserting that filler after a plant "still counts from the device
  offset" therefore could not fail, whatever the generator did, and was rewritten
  to claim only what it checks. The comment is corrected; the pattern is not,
  because its bytes are what the M5 perf baselines were measured over.
- **The `release` preset builds on Linux.** It does not, on GCC 14: an inlined
  `back()` on a vector the optimizer cannot prove non-empty is a
  `-Wnull-dereference` at `-O2`. CI's GCC leg is GCC 13 and stays green, so the
  preset had quietly stopped building on a current Debian. Fixed here because it
  blocked this story's own fixture generation, and recorded because the pattern —
  a newer compiler seeing what CI's does not — is the same one
  [story-0611](story-0611-release-compiles-tests-clang-leg.md) exists for.

  **No test regresses it, and no mutation can pretend otherwise.** `diskBytesFor`
  is a fold now, which is why the diff has no dead guard; but the fold and the
  old `placements.back()` agree on every input any caller can produce — four
  placements at increasing offsets — so breaking the fold fails 22 tests while
  reverting the *fix* fails none. The only thing that catches this class is a
  compiler, and the one that catches this instance is not in CI. That is the
  honest disposition: a compile-only fix, guarded by a toolchain CI does not run.
- **"The parsers have seen twenty seconds of fuzzing."** They had seen twenty
  seconds of *random input generation*. See the instrumentation finding above:
  the number of hours was never the binding constraint.
- **"164 committed corpus inputs", and this story's own re-count of 168.** Both
  counted the working tree with `find`. Only 31 were committed; the rest were
  untracked leftovers `.gitignore` exists to ignore. Recorded above, under the
  count itself, because the lesson belongs next to the number that was wrong.
- **`make_seed_corpus.py` regenerates the tracked seeds.** It regenerates every
  seed it authors, and after this story that includes the thirty-two new ones.
  Two exceptions, of different kinds. `MountTableFuzz`'s four inputs are not
  seeds at all: they arrived with story-0609 out of a fuzz run — each opens with
  a control byte and carries mangled path fragments — so they are minimized
  *finds*, and a generator has no business claiming to write them. That is now
  said in the generator rather than left to be rediscovered.

  The other was a real drift, and it took two wrong answers to land on the right
  one. `Ext4EnumerateFuzz/volume.bin` differed from the generator's output in
  exactly two bytes — offsets `0x5000` and `0x500C`, `0x0C` tracked against
  `0x02` generated. This story's audit first said the drift did not exist, which
  a re-measurement with both sides captured to separate files disproved (65,536
  bytes each, SHA-256 `99928d3a…` against `3c6a3cf7…`). Then it said, correctly,
  that this story had named the wrong *fields*: `0x5000` and `0x500C` are the
  `inode` of the `.` and `..` entries, and `name_len` and `file_type` — which
  the story had blamed — are byte-identical on both sides, as the two-byte diff
  already proved.

  With the right field named, the answer took one line rather than the story
  it was deferred to: `ext4_dir_entry`'s signature carries `inode: int = 12`,
  and `ext4_volume()` passes `inode=2` explicitly. The committed seed holds the
  default, so it predates that argument; and ext4's root directory is inode 2
  by definition, so `.` pointing at 12 was simply wrong. **The seed is
  regenerated here** — the generator was right all along, `Ext4*` stays green
  and `Ext4EnumerateFuzz` loads it at the same coverage. Nothing is deferred,
  and the epic-m7 note this story briefly opened is withdrawn.

## Acceptance criteria

- [x] The library the fuzz targets link carries SanitizerCoverage instrumentation,
      and a gate fails the build when it does not — the campaign below is worth
      nothing without it, and nothing about its absence was visible from outside.
      Gate 13, proven both ways: 0 symbols before, 957 after; exit 1 and exit 0.
- [x] The campaign table is recorded in this story: one row per target — all 29 —
      with CPU-minutes run, total executions, coverage at close, and every finding's
      disposition. 30 CPU-minutes each, 11,046,519,158 executions, no findings.
- [x] Zero open findings at close: every artifact directory is empty, and every real
      fault has its own conventional-commit fix referencing this story plus a unit
      test grown from the minimized reproducer. An OOM or timeout is a finding, not
      noise. **No fault was found, so there is no fix and no test to point at** —
      the second half of this criterion is unexercised rather than met.
- [ ] ~~The merged, minimized corpora are committed for all 29 targets~~; no corpus
      directory holds only a `.gitkeep`; the `fuzz-smoke` job is green over the
      enlarged corpus. **Partly met, and deliberately.** All 29 directories now
      hold tracked seeds and `fuzz-smoke` is green over the merged corpora (29/29,
      20 s each). The 2,786 merged inputs are *not* committed: `.gitignore` tracks
      only curated `*.bin` seeds and ignores libFuzzer's hash-named finds by an
      earlier decision this story is not the place to reverse. With zero crashes
      there is no finding to regress either. See the campaign section.
- [x] The soak ran over a ≥ 256 GiB generated image on the WSL bench; its log — the
      RSS series and the OS-reported peak — is recorded in this story, and the peak
      is within 10% of the same binary's peak over the 128 MiB perf fixture on the
      same bench. 72.1 MiB against 72.1 MiB: 0.0% over a 2,048× larger device.
- [x] The soak run was interrupted at an arbitrary point, resumed, and completed; the
      manifest comparison against an uninterrupted control run shows identical
      recovered-file entries, and the comparison method is recorded with the result.
      256 identical files; the comparator was falsified three ways on this run's
      own data first.
- [x] The new imagegen verb exists, streams (no allocation proportional to image
      size), documents itself in the usage line, and is unit-tested. Streaming is
      a ctest against the OS, at the sizes it ships with: 1.5% growth over a 16×
      larger image, against 429.6% for the buffering `carve` verb.

## Test plan

- Unit (`tests/unit/`, per triaged crash): each minimized reproducer becomes a
  checked-in byte fixture asserting the parser's verdict — the fuzzer found it once;
  the unit test keeps it found even if the corpus were lost.
- Unit (imagegen): the new verb is deterministic, plants exactly the files it was
  asked at the offsets it records, and produces byte-identical output for the same
  arguments — the same bar the existing builders meet. Every one of those claims was
  mutation-checked: the implementation was broken five ways in turn, and each named a
  failing test rather than a broken build.
- ctest (`SoakGeneratorStreams`): the streaming claim is a property of the process,
  not of any function, so it is asked of the OS — peak resident set over two sizes
  sixteen times apart. Proven both ways before it was trusted: at the sizes it
  ships with, the soak verb grows 1.5% and the buffering `carve` verb 429.6%.
  Sixteen times rather than sixty-four because this runs on both platforms in
  three CI jobs and writes the runner's disk every time; the gap it has to
  discriminate is two orders of magnitude wide either way.
- ctest (`SoakUnitTests`): the soak's own verdict function, including the two ways it
  must refuse to pass — two empty manifests are identical, and a plan with no plants
  proves nothing.
- ctest (`LintUnitTests`): the fuzz-instrumentation gate's parsing and its refusal to
  report a pass over an archive it could not inspect.
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
