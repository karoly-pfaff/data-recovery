<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0503: AVX2 prefilter behind a runtime check

- Epic: [epic-m5-performance](../epic-m5-performance.md)
- Status: Backlog
- Size: M

## Goal

Reject the common case — a window position where no signature can start — 32
bytes at a time instead of one. The portable matcher from
[story-0502](story-0502-one-pass-matcher.md) already answers that question with
a single table lookup per byte; this replaces the lookup with a vector compare,
and nothing else.

## Design references

- [strategy.md](../../performance/strategy.md) — the rule this story exists to
  obey: the SIMD path must be behind a runtime feature check, produce
  bit-identical results to the portable path, and be justified by a benchmark.
  "If it is not measurably faster, it does not ship."
- [story-0502](story-0502-one-pass-matcher.md) — the portable matcher and the
  differential harness. Both are prerequisites: an intrinsics path measured
  against a seven-pass matcher would be flattering itself.
- [story-0501](story-0501-benchmark-suite.md) — `scan-simd-vs-portable`, the
  fifth case the [benchmark spec](../../performance/benchmarks.md) lists, which
  only becomes measurable here.
- [io-layer.md](../../architecture/io-layer.md) — the precedent for confining
  platform-conditional code to one place, selected by CMake rather than
  scattered `#ifdef`.

## Scope

1. An **AVX2 implementation of the reject step**, in its own translation unit.
2. **Runtime detection**, performed once, not per window.
3. **`--force-portable`** on both frontends.
4. The **differential test extended** to a third implementation.
5. The **`scan-simd-vs-portable` benchmark case**.

## Design decisions

**Only the reject step is vectorized.** The matcher's structure does not change:
one pass, a cheap "could anything start here" test, an exact comparison for
survivors. The SIMD path answers the cheap test for 32 positions at once and
hands the same survivors to the same verifier. Keeping the two paths one
algorithm with two implementations of one step is what makes bit-identical
output a claim that can be met rather than hoped for.

**The AVX2 code is one translation unit, compiled for AVX2 alone.** Building the
whole binary with `/arch:AVX2` or `-mavx2` would produce something that crashes
on a CPU without it — and the machines people run recovery tools on are old
machines. Only that TU gets the flag (MSVC per-source `/arch:AVX2`; GCC and
Clang either per-source flags or `__attribute__((target("avx2")))`), and nothing
in it is called before the runtime check has passed.

**Detection happens once and is a value, not a global.** `CPUID` is queried when
the matcher's table is built, and the result travels with it. A per-window check
would be measurable in the loop the story exists to speed up, and a lazily
initialized global would be a data race waiting for
[story-0504](story-0504-range-sharding.md).

**`--force-portable` is a product flag, not a test hook.** The benchmark needs
to run both paths on one machine, which is reason enough to build it — but the
reason it stays in a 1.0 is the operator's: if the fast path misbehaves on a
particular CPU, the person whose photographs are on the disk needs a way to turn
it off without waiting for a release. It is documented in
story-0702's man pages like any other flag.

**A third opinion joins the differential test.** story-0502 established the
reference matcher as the oracle. Here all three — reference, portable, AVX2 —
must agree on the same randomized windows, with the AVX2 case skipped (loudly,
not silently) on a machine that cannot run it. A test that quietly passes
because it never executed is worse than no test.

**The measurement is two runs, not two functions.** story-0501 moved the
benchmark outside the process, so `scan-simd-vs-portable` is the same fixture
run twice, once with `--force-portable`, on one machine back to back. The
metric is the ratio, and a ratio divides the machine out — which is what makes
this the one time-based number in the suite worth gating on.

## Acceptance criteria

- [ ] The AVX2 reject step lives in one translation unit, compiled with AVX2
      enabled for that file only, and the rest of the binary runs on a CPU
      without AVX2.
- [ ] Support is detected once, when the matcher's table is built.
- [ ] On a machine without AVX2, the scan runs and produces identical output.
- [ ] `--force-portable` on `revenant-carve` and `revenant-undelete` disables
      the fast path, and is documented in `--help`.
- [ ] Reference, portable and AVX2 matchers produce identical `Match` sequences
      over the seeded randomized windows from story-0502.
- [ ] The differential test reports plainly when the AVX2 case was skipped for
      lack of hardware, and does not count as passed silently.
- [ ] `scan-simd-vs-portable` reports a speedup ratio; the story records it.
- [ ] **The fast path ships only if that ratio is a real win.** A result inside
      the measurement's own spread closes this story as "measured, not shipped",
      with the numbers written down.

## Test plan

Unit (`tests/unit/carve/`): the reject step over a window shorter than one
vector; over a window whose length is not a multiple of the vector width; a
match in the final partial vector; a window with no match.

Differential: story-0502's harness, extended to three implementations.

Golden: the existing carve and hybrid golden tests, run with and without
`--force-portable`, must produce identical output. This is the end-to-end
statement of the same claim the differential test makes about the matcher.

Not automated: the speedup. The suite measures it; the story records it.

## Definition of Done

- [ ] Acceptance criteria met, tests green under ASan + UBSan.
- [ ] clang-format, clang-tidy, duplication and file-length guard clean.
- [ ] `docs/performance/strategy.md` and `benchmarks.md` describe the path that
      now exists, including the measured ratio.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
