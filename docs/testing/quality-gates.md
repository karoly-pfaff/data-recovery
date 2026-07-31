<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Quality Gates

These gates run in CI on every push and pull request. **All must pass to merge.** They
are the mechanical enforcement of [`AGENTS.md`](../../AGENTS.md); none can be merged
around. A gate may only be suppressed inline, at a single site, with a comment
justifying it — and blanket suppressions are rejected in review.

## The gates

| # | Gate | Tool | Fails when… |
|---|------|------|-------------|
| 1 | Formatting | `clang-format --dry-run --Werror` | Any file is not formatted per `.clang-format`. |
| 2 | Static analysis | `clang-tidy` (warnings-as-errors) | Any enabled check fires (naming, size, complexity, bugprone, cppcoreguidelines, …). |
| 3 | File-length guard | `tools/lint/check_file_length.py` | Any source file exceeds the file-length limit. |
| 4 | Duplication (DRY) | `tools/lint/check_duplication.py` (`lizard`) | A block of ≥ 60 tokens is duplicated. See below. |
| 5 | Warnings | compiler `-Wall -Wextra -Werror` / `/W4 /WX` | Any compiler warning on MSVC, GCC, or Clang. |
| 6 | Build matrix | CMake + vcpkg | Build fails on Windows or Linux. |
| 7 | Tests + sanitizers | `ctest` under ASan + UBSan | Any test fails or a sanitizer reports an error. |
| 8 | Coverage floor | `llvm-cov` + `check_coverage.py` | Core-logic line coverage drops below 85%. |
| 9 | Fuzz smoke | libFuzzer (bounded) | A fuzz target crashes/hangs within the time budget. |

## The duplication threshold

Gate 4 fires when a block of **60 tokens or more** is duplicated. Three things
about that are decisions rather than defaults.

**Sixty tokens is one function.** The median function over the scanned roots
(`src include tools`) is 62 tokens, so a block at the bar is a whole typical
function's worth of code living in two places. The number is not converted from
the eight *lines* the previous detector used: lines do not translate into tokens,
and pretending they did would smuggle in an unexamined number. The measurement,
and the command that reproduces it on any later tree, are recorded in
[story-0602](../backlog/stories/story-0602-python-duplication-gate.md).

**The threshold is per copy.** `lizard` sizes a clone family by the tokens of
every copy added together, which lets a wide family of short blocks clear a bar
no single copy comes near. Each copy has to reach it here.

**Only code counts.** A block every one of whose sites lies outside a function
body is not reported. `lizard` unifies identifiers and keywords alike and
collapses literals, so any two runs of layout constants hash the same — and
every byte parser in this tree opens with an include list, a namespace and a
table of on-disk offsets. Those are different facts wearing the only shape C++
has for stating them, and no refactoring makes them one. Duplicated
*declarations* are the [self-audit](../code-quality.md)'s business, not this
gate's.

## What enforces the hard limits

[AGENTS.md §2](../../AGENTS.md#2-hard-limits-enforced-by-clang-tidy--ci-scripts) owns the
numbers. Documentation links to it rather than repeating them; the tool configurations
and the hook encode them because that is the enforcement, not a second source.
This is which check makes each one bite:

| Limit | Enforced by |
|-------|-------------|
| Statements per function, function length, nesting, parameters | `clang-tidy` `readability-function-size` |
| Cognitive complexity | `clang-tidy` `readability-function-cognitive-complexity` |
| File length | `tools/lint/check_file_length.py` (clang-tidy has no file-length check) |

The complexity limit is **cognitive**, not cyclomatic — they are different measures and
give different numbers for the same function. Read the check's name when in doubt.

## Running the gates locally

Run these before pushing:

```bash
cmake --build --preset debug --target format-check
cmake --build --preset debug --target tidy
cmake --build --preset debug --target guard-limits
cmake --build --preset debug --target duplication
ctest --preset debug --output-on-failure
```

**On Windows, run `tidy` from the `release` preset instead** — `cmake --preset release`
once, then `cmake --build --preset release --target tidy` — because clang-tidy cannot
parse the MSVC ASan + `/MDd` debug flag combination.

The pre-commit hook is not a substitute for this. It runs the fast checks only — the
frozen-file guard, the file-length guard and `clang-format` — not `tidy` and not the
tests. See [git-workflow.md](../git-workflow.md) for what it does and how to enable it.

`tidy` checks the whole tree by default, which is what you want locally. CI
splits the same file list across four parallel jobs with
`-DREVENANT_TIDY_SHARDS=4 -DREVENANT_TIDY_SHARD=<n>`, because clang-tidy visits
every file and one runner's cores are the ceiling on how fast that goes. The set
of files checked is identical either way — only the number of machines changes —
and a shard index outside the range fails the configure rather than silently
leaving files unchecked. Change the matrix size and `REVENANT_TIDY_SHARDS`
together.

## Sanitizer policy

- ASan + UBSan run together in the `debug` preset and in CI; `-fno-sanitize-recover=all`
  makes any finding a hard failure.
- TSan runs in a dedicated Linux job for concurrency-touching code (threaded scanning).
- Sanitizer findings are bugs, never "expected"; there is no suppression file for them
  without an ADR.

## Changing a gate

Gates are part of the contract. Tightening or loosening one is a dedicated PR that
updates `AGENTS.md`, this file, and the relevant tool config together, with rationale.
Never weaken a gate to get an unrelated change through.
