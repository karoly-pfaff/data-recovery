<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0006: `check_coverage.py` + coverage gate at 85%

- Epic: [epic-m0-foundation](../epic-m0-foundation.md)
- Status: Done
- Size: S

## Goal

Enforce the 85% core-coverage floor mechanically: a checker script over llvm-cov
JSON and a CI job that fails below the floor.

## Acceptance criteria

- [x] `tools/lint/check_coverage.py` fails on <85% core-logic coverage and passes
      on ≥85%, verified against committed fixture JSONs via ctest.
- [x] The gate fails hard when no core files match the given `--prefix` values —
      a silently-empty gate would be a fake gate.
- [x] The CI `coverage` job produces real `llvm-cov` data (Clang build + profraw
      merge, not GCC `--coverage`) and runs the script against it.

## Test plan

- `CoverageGatePassesAboveFloor`: runs the script against
  `tests/fixtures/coverage/passing.json` (core coverage 93.6%), expected to pass
  on exit code (default ctest pass/fail semantics — exit 0 is correct here).
- `CoverageGateFailsBelowFloor`: runs the script against
  `tests/fixtures/coverage/failing.json` (core coverage 42.9%), asserted via
  `PASS_REGULAR_EXPRESSION` matching the exact "core line coverage: 42.9%
  (floor 85.0%)" line the script prints on a genuine below-floor evaluation.
- `CoverageGateRefusesEmptyMatch`: runs the script against
  `passing.json` with `--prefix nonexistent` (zero files match), asserted via
  `PASS_REGULAR_EXPRESSION` matching "refusing to pass an empty gate".
- Manual: script run by hand on both fixtures, and on a `--prefix` that matches
  zero files, to confirm the "empty gate" hard-error path (see report).

### Known issue (fixed) — `WILL_FAIL` masked regression risk

The first cut of this wiring used `set_tests_properties(... PROPERTIES
WILL_FAIL TRUE)` on the below-floor case: ctest inverts the exit code, so any
non-zero exit — whether from a genuine below-floor evaluation *or* an
unrelated crash (e.g. a deleted/corrupted fixture, or a future refactor that
broke the below-floor branch specifically) — reported as "Passed" with no way
to tell the two apart. The empty-match hard-fail path also had no automated
regression coverage at all, only the manual invocation captured in the task
report. Both gaps were flagged in review and fixed by switching to
`PASS_REGULAR_EXPRESSION` (matching the script's exact success-path or
refusal-path output text) instead of `WILL_FAIL`, and adding
`CoverageGateRefusesEmptyMatch` as its own ctest case. `WILL_FAIL` combined
with `PASS_REGULAR_EXPRESSION` was considered and rejected: CTest defines
`PASS_REGULAR_EXPRESSION` to override exit-code-based pass/fail entirely, so
stacking `WILL_FAIL` on top would invert a correct regex match into a
reported failure. Verified by deliberately breaking each fixture (temporary
rename, not committed) and confirming the corresponding ctest case now fails
for real — see the task report's fix-report section for transcripts.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan+UBSan; the three
      `CoverageGate*` ctest cases plus the existing 37).
- [x] Coverage held or raised (>= 85% core). This story is what makes that
      checkbox mechanically meaningful for every story from here on — it was
      ticked on precedent in stories 0001–0005 because no gate existed yet.
      No C++ production code changed in this story, so there is nothing new
      to measure; the gate itself is now real and enforced in CI.
- [x] clang-format, clang-tidy, duplication, file-length guard clean. No C++
      changed, so `format-check` and `guard-limits` are trivially green (run
      anyway, see report); `tidy` was not run (no C++ change to analyze).
      Python has no project lint gate yet; the script is kept well under the
      250-line file guard on principle.
- [x] CHANGELOG.md updated under [Unreleased].
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Docs/ADRs updated if the design changed (no design change; N/A).

## Notes

The script only trusts `llvm-cov export -summary-only` JSON: `data[0].files[*]`
entries with a `.summary.lines.covered`/`.count` pair. Filenames are matched
against `--root` and then against one or more `--prefix` values (repeatable);
only matching files count toward the numerator/denominator, so `tests/` and
`tools/` sources never inflate or dilute the core-logic figure.

The CI `coverage` job (`.github/workflows/ci.yml`) builds under Clang with
`LLVM_PROFILE_FILE` pointing at `build/coverage/raw/%p.profraw`, merges the
raw profiles with `llvm-profdata merge -sparse`, exports summary JSON with
`llvm-cov export -summary-only`, and feeds that straight to
`tools/lint/check_coverage.py --min 85 --prefix src --prefix include`. This
replaces the previous placeholder step that only echoed a TODO comment.
