<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Quality Gates

These gates run in CI on every push and pull request. **Gates 1–11 must all pass to
merge.** They are the mechanical enforcement of [`AGENTS.md`](../../AGENTS.md); none can
be merged around. A gate may only be suppressed inline, at a single site, with a comment
justifying it — and blanket suppressions are rejected in review.

Gate 12 is deliberately not one of them. It reports, a finding does not stop a merge, and
it runs on a schedule and on pull requests rather than on every push. It is in the table
anyway, because a check nobody wrote down is a check nobody reads.

## The gates

| # | Gate | Tool | Fails when… |
|---|------|------|-------------|
| 1 | Formatting | `clang-format --dry-run --Werror` | Any file is not formatted per `.clang-format`. |
| 2 | Static analysis | `clang-tidy` (warnings-as-errors) | Any enabled check fires (naming, size, complexity, bugprone, cppcoreguidelines, …). |
| 3 | File-length guard | `tools/lint/check_file_length.py` | Any source file exceeds the file-length limit. |
| 4 | Duplication (DRY) | `tools/lint/check_duplication.py` (`lizard`) | A block of ≥ 60 tokens is duplicated. See below. |
| 5 | Warnings | compiler `-Wall -Wextra -Werror` / `/W4 /WX` | Any compiler warning on MSVC, GCC, or Clang. See below for the configurations it is enforced in. |
| 6 | Build matrix | CMake + vcpkg | Build fails on Windows or Linux. |
| 7 | Tests + sanitizers | `ctest` under ASan + UBSan | Any test fails or a sanitizer reports an error. |
| 8 | Coverage floor | `llvm-cov` + `check_coverage.py` | Core-logic line coverage drops below 85%. |
| 9 | Fuzz smoke | libFuzzer (bounded) | A fuzz target crashes/hangs within the time budget. |
| 10 | Source encoding | `tools/lint/check_encoding.py` | Any source file is not plain UTF-8, or carries a byte-order mark. |
| 11 | Layer direction | `tools/lint/check_layering.py` | A file includes a header from a layer *above* its own. See below. |
| 12 | Taint analysis | CodeQL, `security-and-quality` | Never on a finding — it reports. The job fails only when the build or the database does. **CI-only, non-blocking**; see below. |

## The layer DAG, and what "below" means

Gate 11 enforces the one structural claim the architecture makes:
[the overview](../architecture/overview.md#layered-design)'s "each layer depends only on
the layer below". Until it existed nothing checked that — one static library holds every
layer and the whole of `src/` is an include path for all of it, so the compiler cannot
object, and `clang-tidy`'s `misc-header-include-cycle` finds *cycles*, which an inverted
but acyclic edge is not.

The rule is **direction, not adjacency**: a file in layer *L* may include any layer at or
below *L*. The allowed relation is the transitive closure of

```
tools → cli → recovery → carve → fs → volume → core
```

Read literally, "the layer below" would condemn most of the codebase — of the tree's
cross-layer edges only a minority land on the immediately-lower layer, and every
`fs → core` and `carve → core` among them skips a rung. A gate that has to be argued with
on day one is a gate that gets switched off, so what is checked is that the arrows point
one way.

Two departures from the diagram, both measured rather than assumed. `core/io` folds into
`core`, because no file in `core/` outside `io/` includes `core/io/` — splitting the node
would buy a sub-directory rule the tree has never needed. And `tools` is added *above*
`cli`, because `tools/imagegen` consumes the library exactly as the frontends do; that
buys one rule worth having, which is that nothing in `src/` or `include/` may include the
fixture builders. `tests/` is not walked at all: a unit test's job is to reach whatever it
tests, private headers included, so every edge there is permitted and walking it would be
dead code.

The layer order lives in one list in the script, cited to the diagram. It is not a config
file, because a second machine-readable copy of the stack is a second thing to drift —
and changing the diagram without changing the list is a review finding, not something the
gate can catch. A file under a walked root whose top-level directory the list does not
name stops the gate naming it, rather than being skipped.

## Which configurations the warning contract is enforced in

Gate 5 promises "any compiler warning on MSVC, GCC, or Clang". A warning is a
property of a *configuration*, not of a compiler — `-Wnull-dereference` and
`-Warray-bounds` see almost nothing without an optimizer — so the promise is only
as wide as the builds behind it:

| Job | Compiler | Configuration | `src`/`tools` | test TUs |
|-----|----------|---------------|:---:|:---:|
| `build-test` (ubuntu) | GCC | Debug, ASan + UBSan | yes | yes |
| `build-test` (windows) | MSVC | Debug, ASan + UBSan | yes | yes |
| `coverage` | Clang | Debug + instrumentation | yes | yes |
| `fuzz-smoke` | Clang | Debug + libFuzzer | yes | yes |
| `build-release` (artifact) | GCC | RelWithDebInfo | yes | yes |
| `build-release` (clang optimized) | Clang | RelWithDebInfo | yes | yes |

Every cell was `no` for test translation units in the optimized row until
[story-0611](../backlog/stories/story-0611-release-compiles-tests-clang-leg.md), and
there was no optimized Clang row at all. Neither optimized leg runs `ctest`: the
debug legs run the suite under sanitizers, which is the stronger dynamic check, and
what these two buy is the compiler's opinion — which is delivered by compiling.
Because a build that compiles nothing would satisfy that vacuously, each leg asserts
the test binary exists afterwards.
## Which job runs which gate, and where

A gate is only as wide as the platforms that run it, so this says so per gate rather than
leaving it to be read off `ci.yml`. Two of them are invoked as the literal CMake target a
contributor runs locally — the rest are scripts or compilers CI calls directly.

| # | Gate | Job | Linux | Windows |
|---|------|-----|:-----:|:-------:|
| 1 | Formatting | `guards` + `build-test` (windows) — the `format-check` **target** | yes | yes |
| 2 | Static analysis | `tidy` ×4 — the `tidy` **target** | yes | **no** |
| 3 | File-length guard | `guards` + `build-test` (windows) — the `guard-limits` **target** | yes | yes |
| 11 | Layer direction | the same `guard-limits` **target**, so the same two jobs | yes | yes |
| 4 | Duplication | `guards` | yes | **no** |
| 5 | Warnings | every build job — see the configuration table above | yes | yes |
| 6 | Build matrix | `build-test` | yes | yes |
| 7 | Tests + sanitizers | `build-test` | yes | yes |
| 8 | Coverage floor | `coverage` | yes | **no** |
| 9 | Fuzz smoke | `fuzz-smoke` | yes | **no** |
| 10 | Source encoding | `guards` | yes | **no** |
| 12 | Taint analysis | `codeql` (a separate workflow) — **CI-only: there is no local target** | yes | **no** |

**Why the Linux-only ones stay that way.** Gate 2 cannot run from the Windows debug
preset at all: clang-tidy rejects the MSVC ASan + `/MDd` combination, so the local Windows
procedure runs it from the `release` preset — a second configure and an entire extra
optimized Windows build, which the run's budget will not take. Gates 4, 8, 9 and 10 ask
questions with no platform dimension: whether two blocks of C++ are the same text, what
fraction of lines a test suite reached, whether a parser survives hostile bytes, and
whether a file is valid UTF-8. Running them twice would double their cost and could not
produce a different answer. Gates 1 and 3 *are* platform-dependent — the format target
died on every Windows invocation for a milestone because of a command-line length limit
Linux does not have — which is why those two, and only those two, are invoked on both.

## Gate 12: CodeQL reports, it does not gate

**What it asks that nothing else here does.** Every other gate works inside one
translation unit or one input: clang-tidy sees a file at a time, the fuzzers see what
their corpus reaches. Neither follows a length field from `BlockDevice::readAt` through
three functions into an allocation size, which for a tool whose entire job is parsing
bytes a failing disk or an attacker chose is the question worth asking. The overflow
guards in `src/core/SafeArith.hpp` are each there because a person noticed.

**It already treats a disk read as untrusted, and no configuration was needed to get
there.** CodeQL models `fread` as a *remote* flow source, so the default threat model
covers the case this project cares about; setting `threat-models: [ local ]` was tried
against a planted finding and changed the result not at all.

**CI-only, said out loud rather than discovered.** There is no
`cmake --build --target codeql`. The analysis needs the CodeQL CLI and a database built
by observing a from-scratch compile of the whole tree — minutes of work, and a toolchain
no contributor is asked to install.
[story-0612](../backlog/stories/story-0612-ci-runs-gate-targets.md) found the shape where
CI reimplements a gate a developer runs locally and the two drift; the fix there was to
make CI run the real target. This one has no local target to drift from, so the table
says so, instead of leaving the next person to hunt for one that was never written. What
a contributor can do locally is read the alerts, which are per-branch in the Security tab.

**Non-blocking, and exactly what that means.** The job's success does not depend on what
CodeQL finds; it fails only when the build breaks or the database comes back short of the
tree. GitHub separately attaches a second check named `CodeQL` to a pull request — from
the Advanced Security app rather than from this workflow, so the two sit side by side —
and it can mark *that* one failed when the PR introduces a high-severity alert. It still
cannot stop a merge, because it is not a *required* check: the `protect-main`
ruleset refuses a deletion and a force-push, and requires no check to pass. Promoting this
to a gate is therefore a deliberate act — adding the check to that ruleset, once the first
runs have shown what the signal-to-noise actually is — and it gets a story number rather
than a quiet settings change. What is not optional in the meantime is reading it: an alert
is fixed, or dismissed in the Security tab with a stated reason, or it becomes a story.

**What it builds, and when.** Pull requests targeting `main`, a weekly schedule, and
manual dispatch — not every push, because each run pays for a full build of the tree. It
configures with tests off, which is also what keeps the job free of vcpkg: the only
`find_package` here is GTest, reached only when `REVENANT_BUILD_TESTS` is ON. So `src/`,
`include/` and `tools/` are analysed and the test suite is not. And because a green run
over an empty database is indistinguishable from a green run over a clean one, the job
compares CodeQL's own source archive against `compile_commands.json` and fails if the
database did not see what CMake compiled.

## The duplication threshold

Gate 4 fires when a block of **60 tokens or more** is duplicated. Three things
about that are decisions rather than defaults.

**Sixty tokens is one function.** The median function in the files the gate scans
(the `.cpp`/`.hpp` under `src include tools`) is 62 tokens, so a block at the bar
is a whole typical function's worth of code living in two places. The number is
not converted from the eight *lines* the previous detector used: lines do not
translate into tokens, and pretending they did would smuggle in an unexamined
number. The measurement, and the command that reproduces it on any later tree,
are recorded in
[story-0602](../backlog/stories/story-0602-python-duplication-gate.md).

**The threshold is per copy.** `lizard` sizes a clone family by the tokens of
every copy added together, which lets a wide family of short blocks clear a bar
no single copy comes near. Each copy has to reach it here.

**Only code counts.** A block is reported only when *every one* of its sites
reaches a function body; one site that is all declarations drops the whole
family. Reaching rather than lying inside, because a match runs in windows of
tokens and routinely starts a few lines above the function it is really about.
`lizard` unifies identifiers and keywords alike and collapses literals, so any
two runs of layout constants hash the same, and every byte parser in this tree
opens with an include list, a namespace and a table of on-disk offsets. Those
are different facts wearing the only shape C++ has for stating them, and no
refactoring makes them one. Duplicated *declarations* are the
[self-audit](../code-quality.md)'s business, not this gate's.

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
cmake --build --preset debug --target guard-limits   # length ceiling + layer DAG
cmake --build --preset debug --target duplication
cmake --build --preset debug --target encoding
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
