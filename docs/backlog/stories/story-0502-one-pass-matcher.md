<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0502: One pass over the window, not one per signature

- Epic: [epic-m5-performance](../epic-m5-performance.md)
- Status: Done
- Size: L

## Goal

Match every registered signature in a single pass over each window. The scanner
currently makes one full pass per signature, so the hottest loop in the project
reads every byte of the device seven times over — and the fix is portable
arithmetic, not an instruction set.

## Design references

- [`src/carve/WindowMatch.cpp`](../../../src/carve/WindowMatch.cpp) — the matcher
  being replaced: `matchesInWindow` loops carvers, then signatures, and calls
  `findAll`, which restarts `std::ranges::search` after every hit.
- [strategy.md](../../performance/strategy.md) — "Baseline: a correct, portable
  multi-pattern matcher (Aho-Corasick-style) over streamed windows." That
  sentence describes a matcher this story is the first to actually build.
- [story-0501](story-0501-benchmark-suite.md) — the measurement this is
  accepted against. It lands first for that reason.
- [ADR-0003](../../architecture/adr/adr-0003-validating-carving.md) — a
  signature hit is a hypothesis, never a file. This story changes only how
  hypotheses are *found*; every one of them still goes to its carver.

## The shape of the problem

Seven signatures are registered today — JPEG, PNG, MP4, PDF, ZIP one each, and
two for RAW (little- and big-endian TIFF). Their magics are two to eight bytes.
One of them, MP4's `ftyp`, sits at `Signature::offset == 4` rather than at the
start of the file, which is why `Signature` carries an offset at all and why the
matcher cannot assume a hit is a candidate start.

So the current cost is seven generic searches over every window — 28 MiB of
comparisons per 4 MiB window — and it grows with every format added. A carver
is supposed to be cheap to add; today each one taxes every byte of every disk.

## Scope

1. A **multi-pattern matcher** that visits each window position once and reports
   every signature that matches there.
2. Whatever precomputed table it needs, built **once** — owned by
   `CarverRegistry`, which already answers the combined-signature questions
   (`maxSignatureBytes`), rebuilt only when a carver is registered.
3. A **reference matcher** in `tests/support/`: the current nested-search
   implementation, kept as the definition of correct.
4. A **differential test** proving the two agree.

## Design decisions

**The old matcher becomes the oracle, not deleted history.** Correctness here is
not "the carve tests still pass" — those exercise a handful of planted headers.
It is "the same set of `Match` values, in the same order, for any bytes at all",
and the only honest way to assert that is against an implementation whose
simplicity makes it obviously right. It moves to `tests/support/` and stays.
[story-0503](story-0503-avx2-prefilter.md) needs the same oracle, so building it
here costs that story nothing.

**A byte-class table, then verify — not Aho-Corasick.** The doc says
"Aho-Corasick-style", and the honest reading of that is "one pass, all
patterns", not a specific automaton. With seven short patterns, Aho-Corasick's
per-byte pointer chase through a trie is a poor trade: the table is bigger than
the working set it protects, and the common case is *no match at all*. A
256-entry table keyed by the byte, holding a bitmask of which signatures could
start with it, answers "nothing here" in one load and one test, and hands the
rare survivor to an exact comparison. It is also the exact shape
[story-0503](story-0503-avx2-prefilter.md) vectorizes, so the portable path and
the SIMD path stay the same algorithm with two implementations of one step —
which is what makes bit-identical output a reasonable thing to demand.

If the benchmark disagrees, the benchmark wins and the story records why.

**The match order stays the contract.** `matchesInWindow` sorts by offset today,
and everything downstream depends on it: `processMatches` walks matches in order
and skips those falling inside an extent a previous candidate resumed past. A
matcher that emits in a different order silently changes which candidates get
carved. So ordering is part of what the differential test asserts, not an
implementation detail.

**No allocation in the per-window path.** The table is built once; the match
vector reuses its capacity across windows. ADR-0009's bounded-allocation rule
applies to the hot loop as much as to parsers.

## Acceptance criteria

- [x] The matcher makes one pass over the window regardless of how many
      signatures are registered, and adding an eighth signature does not add a
      pass.
- [x] Its table is built when carvers are registered, not per window.
- [x] For randomized windows over a seeded generator, the new matcher and the
      reference produce identical `Match` sequences — same offsets, same
      carvers, same order.
- [x] The differential cases include, explicitly: a magic at a non-zero
      `Signature::offset`; two signatures matching at the same position;
      overlapping occurrences of one magic; a magic in the first and last bytes
      of a window; and a hit whose implied candidate start would underflow
      (`absolute < signature.offset`), which must yield no match.
- [x] Every existing carve, hybrid and golden test passes unchanged.
- [x] `scan-throughput` improves measurably on the benchmark suite, reported in
      the story on completion. No improvement means the story does not land.
- [x] The per-window path performs no heap allocation beyond reusing the match
      vector's capacity.

## Test plan

Unit (`tests/unit/carve/`): the table maps each registered signature's first
byte; a window with no match at all reports nothing; a match at position 0; a
match ending on the window's last byte; two carvers whose magics share a first
byte are both reported.

Differential (`tests/unit/carve/MatcherDifferentialTest.cpp`): the cases above
plus randomized windows from a seeded generator, asserted against the reference
matcher. The seed is fixed and printed on failure, so a red run is reproducible.

Fuzz: the existing carve fuzz targets cover the parsers. A new target is not
required — the matcher's input is our own window buffer, not device-controlled
structure — but the differential test is the equivalent guarantee and is
required.

Not automated: the throughput number. It is machine-dependent; the suite
measures it and the story records what it measured.

## What it measured

`scan-throughput` on the Windows workbench, five repetitions, 128 MiB image:

| Matcher | Rate | Median | Spread |
|---------|------|--------|:------:|
| Seven passes per window (before) | 643.4 MiB/s | 0.199 s | 2.6% |
| One pass, table lookup out of line | 296.6 MiB/s | 0.432 s | 1.8% |
| One pass, one-load reject out of line | 599.1 MiB/s | 0.214 s | 2.2% |
| **One pass, reject inlined (shipped)** | **831.4 MiB/s** | **0.154 s** | **3.1%** |

The two middle rows are the interesting part, because the first attempt was
*slower than what it replaced*, and the reason was not the algorithm:

- **The MSVC standard library vectorizes `std::ranges::search` over byte ranges
  and libstdc++ does not.** Seven vectorized passes beat one scalar pass, which
  is why this had to be measured on both platforms rather than argued about on
  one. Linux measured 327 MiB/s before this story, against Windows' 643 on
  slower hardware.
- **A cheap question asked across a translation-unit boundary is not cheap.**
  The project links without LTO, so `SignatureTable::none` out of line was a
  *call* per byte of the device rather than a load: 599 MiB/s against 831 for
  the same code inlined in the header. Half the throughput of the scanner sat in
  one function's linkage.

The other three cases are unchanged within the gate's thresholds:
`carve-validate` 1 627 → 1 636 candidates/s, `ntfs-enumerate` 3 307 → 3 224
entries/s, `end-to-end-hybrid` 38.0 → 38.4 MiB/s. None of them is
signature-scanning bound — `carve-validate` is dominated by the per-candidate
device re-read [story-0501](story-0501-benchmark-suite.md) recorded as
quadratic, and the other two by filesystem metadata.

### Where the design departed from the story

The story specified "a 256-entry table keyed by the byte, holding a **bitmask**
of which signatures could start with it". It ships as a per-byte flag plus a
group index instead, for one reason: a bitmask of signature indices caps the
registry at as many signatures as the mask has bits, and the 65th carver would
have to fail inside `registerCarver`, which returns `void` and has nowhere to
report it. What the story actually asked for — one load and one test for the
common answer — is unchanged, and it is still the shape
[story-0503](story-0503-avx2-prefilter.md) vectorizes.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] clang-format, clang-tidy, duplication and file-length guard clean.
- [x] `docs/performance/strategy.md` describes the matcher that now exists.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
