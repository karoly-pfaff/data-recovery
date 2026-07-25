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
| 3 | File-length guard | `tools/lint/check_file_length.py` | Any source file exceeds 250 lines. |
| 4 | Duplication (DRY) | `jscpd` | Duplicated blocks ≥ 8 lines are found. |
| 5 | Warnings | compiler `-Wall -Wextra -Werror` / `/W4 /WX` | Any compiler warning on MSVC, GCC, or Clang. |
| 6 | Build matrix | CMake + vcpkg | Build fails on Windows or Linux. |
| 7 | Tests + sanitizers | `ctest` under ASan + UBSan | Any test fails or a sanitizer reports an error. |
| 8 | Coverage floor | `llvm-cov` + `check_coverage.py` | Core-logic line coverage drops below 85%. |
| 9 | Fuzz smoke | libFuzzer (bounded) | A fuzz target crashes/hangs within the time budget. |

## Enforced limits (mirror of AGENTS.md §2)

| Rule | Warn | Hard fail |
|------|:----:|:---------:|
| File length (lines) | 200 | **250** |
| Statements / function | 8 | **10** |
| Cyclomatic / cognitive complexity | 8 | **10** |
| Nesting depth | 3 | **4** |
| Parameter count | 4 | **5** |
| Function length (lines) | 40 | **60** |

Gates 2 and 3 enforce these: `clang-tidy`'s `readability-function-size` and
`readability-function-cognitive-complexity` cover functions; the file-length script
covers files (clang-tidy has no file-length check).

## Local pre-flight

Run the gates before pushing (also wired as a pre-commit hook):

```bash
cmake --build --preset debug --target format-check
cmake --build --preset debug --target tidy
cmake --build --preset debug --target guard-limits
ctest --preset debug --output-on-failure
```

On Windows, run the `tidy` target from the `release` preset instead
(`cmake --preset release` once, then `cmake --build --preset release --target
tidy`), because clang-tidy cannot parse the MSVC ASan + `/MDd` debug flag
combination.

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
