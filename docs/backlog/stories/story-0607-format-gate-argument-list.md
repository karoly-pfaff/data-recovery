<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0607: The format gate dies of its own argument list on Windows

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: Done
- Size: S

## Goal

`format-check` and `format` pass every source file to clang-format as the arguments
of a single command. The tree has outgrown that: the generated command line is
32,997 characters against Windows' 32,767-character `CreateProcess` limit, so both
targets die with `The system cannot execute the specified program.` before
clang-format runs — on every invocation, regardless of what changed. Make the format
targets run on both development platforms, and keep them failing for formatting
reasons only.

## Design references

- [`cmake/DevTargets.cmake`](../../../cmake/DevTargets.cmake) — one
  `add_custom_target` per gate; `${revenant_sources}` expands into the argument
  list of both `format` and `format-check`.
- [`.claude/agents/gate-runner.md`](../../../.claude/agents/gate-runner.md) — where
  the failure was found (the first full local gate run after M5 closed, 2026-07-30)
  and where the interim fallback lives: check the diff's files directly.
- [story-0602](story-0602-python-duplication-gate.md) — the direction gate scripts
  move in this repository: Python, tested, owned by `tools/lint/`.

## What was measured

At the tree of v0.3.0, the generated runner
(`build/debug/CMakeFiles/format-check-*.bat`) carries a 32,997-character line. The
limit is fixed and the tree only grows, so this is not flaky: the target cannot run
on Windows again without this story. CI is unaffected — Linux's argument limit is
megabytes — which is exactly why it went unnoticed. A local gate had quietly become
a CI-only gate, while [AGENTS.md](../../../AGENTS.md) §6 still lists it as something
every change passes locally.

## Design decisions

- **The failure mode must be a formatting verdict, never a launcher error.**
  Whatever the mechanism — a response file (`clang-format @sources.rsp`), batched
  invocations, or a `tools/lint/` Python driver in the story-0602 mold — a
  formatting violation exits non-zero naming the file, and a clean tree exits zero,
  on both platforms.
- **One mechanism for both targets.** `format` (in-place) has the identical defect;
  fixing only the check leaves the fixer broken.
- **The covered file set does not change.** `src`, `include`, `tests`, `tools` —
  the story changes how the list is delivered, not what is on it.

## Acceptance criteria

- [x] `cmake --build --preset debug --target format-check` reaches a real verdict on
      Windows and Linux at the current tree size, with no fixed-size command line
      anywhere in the mechanism to outgrow again.
- [x] A deliberately misformatted file makes `format-check` fail naming the file; a
      clean tree passes. Verified on both platforms.
- [x] `format` reformats the same file set through the same mechanism.
- [x] The gate-runner's trap entry and `(fallback: diff files only)` note for this
      failure are removed — the fallback exists only because the target cannot run.

## Test plan

- If the mechanism lands as a Python driver: unit tests in `tests/unit/lint/` over
  fixture files — a clean pair passes, a violation fails naming the file — like the
  coverage gate's fixture tests.
- If it stays inside CMake (a response file): the acceptance criteria are the test,
  recorded in this story on completion for both platforms; there is no unit seam in
  an `add_custom_target`.
- Not automated: the 32,767 limit itself. "No fixed-size command line" is reviewed,
  not measured.

## Verified on completion (2026-07-30)

The mechanism landed as the Python driver: `tools/lint/check_format.py` discovers the
file set at run time and batches it under a 24,000-character default budget (the
`CreateProcess` ceiling minus generous headroom), so growth adds invocations rather
than length. Unit tests cover discovery, batching, the verdict, the check/fix flag
split, the missing-root refusal, and the empty-set refusal
(`tests/unit/lint/test_check_format.py`, run by the `LintUnitTests` ctest entry). One
deviation from the plan as written, on purpose: the tests drive an injected runner seam
rather than real clang-format over fixture files — clang-format's own verdict is not
ours to test — and the real-tool behavior is covered by the recorded per-platform runs
below. File discovery is shared with the file-length guard through
`tools/lint/source_set.py`, so "which files do the gates cover" has one answer, and a
root that does not exist fails the gate instead of quietly shrinking it.

- **Windows** (debug preset, this machine): `format-check` reaches a verdict — "format
  gate: clean", exit 0 — where it previously died in the launcher. A planted
  `src/story0607_negcase.cpp` (`int   main( ){return 0;}`) fails the gate at exit 1
  naming the file (`story0607_negcase.cpp:1:7: error: code should be clang-formatted`);
  `format` reformats it in place; the tree re-checks clean after removal.
- **Linux** (WSL bench, `cmake -G Ninja -DREVENANT_BUILD_TESTS=OFF`, clang-format
  pinned at 22.1.8 to match CI): the same three checks — clean exit 0, planted
  misformat exit 1 naming the file, clean again after removal.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan (997/997 + the two
      gate-refusal ctest entries).
- [x] clang-format, clang-tidy, duplication and file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md))
      completed — first round REWORK (four findings, all resolved: shared
      `source_set.py`, missing-root refusal, YAGNI trims, `LintUnitTests` rename),
      second round READY.
