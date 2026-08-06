<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Quality Gates

These gates run in CI on every push and pull request. **Gates 1–11 and 13 must all pass
to merge.** They are the mechanical enforcement of [`AGENTS.md`](../../AGENTS.md). A gate may
only be suppressed inline, at a single site, with a comment justifying it — and blanket
suppressions are rejected in review.

"Must pass" is a project rule, not something the repository enforces on its own: the
`protect-main` ruleset refuses a deletion and a force-push and requires no check to pass,
so a red PR is *merge-able* and merging one is simply not done. Anyone who wants the rule
enforced mechanically adds the checks to that ruleset, which is a change to make on
purpose rather than a thing to assume is already true.

Gate 12 is the one exception. It reports, a finding does not stop a merge even by
convention, and it runs on a schedule and on pull requests rather than on every push. It
is in the table anyway, because a check nobody wrote down is a check nobody reads. Gate 13
is not an exception: it arrived after gate 12 and blocks like the rest, because a fuzz gate
whose feedback loop is disconnected reports the same green as a working one.

## The gates

| # | Gate | Tool | Fails when… |
|---|------|------|-------------|
| 1 | Formatting | `clang-format --dry-run --Werror` | Any file is not formatted per `.clang-format`. |
| 2 | Static analysis | `clang-tidy` (warnings-as-errors) | Any enabled check fires (naming, size, complexity, bugprone, cppcoreguidelines, …). |
| 3 | File-length guard | `tools/lint/check_file_length.py` | Any C++ **or Python** source file exceeds the file-length limit. |
| 4 | Duplication (DRY) | `tools/lint/check_duplication.py` (`lizard`) | A block of ≥ 60 tokens is duplicated, in C++ **or Python**. See below. |
| 5 | Warnings | compiler `-Wall -Wextra -Werror` / `/W4 /WX` | Any compiler warning on MSVC, GCC, or Clang. See below for the configurations it is enforced in. |
| 6 | Build matrix | CMake + vcpkg | Build fails on Windows or Linux. |
| 7 | Tests + sanitizers | `ctest` under ASan + UBSan | Any test fails or a sanitizer reports an error. |
| 8 | Coverage floor | `llvm-cov` + `check_coverage.py` | Core-logic line coverage drops below 85%. |
| 9 | Fuzz smoke | libFuzzer (bounded) | A fuzz target crashes/hangs within the time budget. |
| 10 | Source encoding | `tools/lint/check_encoding.py` | Any C++ or Python source file is not plain UTF-8, or carries a byte-order mark. |
| 11 | Layer direction | `tools/lint/check_layering.py` | A file includes a header from a layer *above* its own. See below. |
| 12 | Taint analysis | CodeQL, `security-and-quality` | Never on a finding — it reports. The job fails only when the build or the database does. **CI-only, non-blocking**; see below. |
| 13 | Fuzz instrumentation | `tools/lint/check_fuzz_instrumentation.py` | The library the fuzz targets link carries no SanitizerCoverage symbols, so gate 9 is mutating blind. **Blocks a merge**, and runs before gate 9 in the same job. See below. |

## Which gates read which language

The five gates that walk the tree share their file discovery
(`tools/lint/source_set.py`), and **each states the suffixes it can analyse**
rather than all reading one global set (story-0703):

| Gate | C++ | Python | Why |
|------|:---:|:------:|-----|
| File-length guard | yes | yes | AGENTS.md §2 states the limit without naming a language, and "too many responsibilities" is language-independent. |
| Duplication | yes | yes | `lizard` tokenizes both; duplicated *knowledge* is the target. |
| Source encoding | yes | yes | A stray non-UTF-8 byte is a defect in any text file. |
| Formatting | yes | **no** | It runs `clang-format`, which does not format Python. A Python formatter would be a new gate and a separate decision. |
| Layer direction | yes | **no** | The layer DAG is a statement about C++ includes; `import` is not one. |

The two "no" rows are why the suffix set is per gate. Widening one shared
constant — the obvious change — would have handed Python to `clang-format` and
to the include-DAG parser, which is how a gate starts reporting on something it
cannot analyse.

`__pycache__` is never discovered, so a gate cannot fail on a byte-compiled file
nobody can fix in the source.

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
| 13 | Fuzz instrumentation | `fuzz-smoke`, before gate 9 runs | yes | **no** |

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

Gate 13 is Linux-only because the thing it inspects exists nowhere else: the fuzz
build is Clang-and-Linux by construction (the `fuzz` preset), so there is no Windows
archive to ask about. Its *verdict* is platform-independent and unit-tested as such —
`LintUnitTests` runs on both platforms and injects the symbol reader, because `nm` is a
detail of how the gate looks rather than of what it decides.

Gate 12 is Linux-only for a different reason again: a second platform would mean a second
full build and a second database to answer a question about source code, and CodeQL's C++
analysis does not change its mind about a taint path because MSVC compiled it. The
platform-specific halves of `core/io/` are the one real cost — `RawDeviceWindows.cpp` and
its neighbours are compiled by no CodeQL run, so they are unanalysed. That is a known hole,
not an oversight.

## Gate 13: what gate 9 was actually measuring

`-fsanitize=fuzzer` instruments the translation unit it is applied to. It was applied
to each fuzz target and to nothing else, so `librevenant` — which holds every parser
any of them exists to test — was compiled with no SanitizerCoverage at all. libFuzzer's
whole method is to keep inputs that reach new code; with no counters in the code under
test it kept almost nothing, and gate 9 spent its twenty seconds per target generating
near-random bytes. Thirteen counters for `JpegCarverFuzz`, forty-five for
`NtfsEnumerateFuzz`: the harness files, and not one parser.

Nothing about that state was visible from outside. The targets built, ran the corpus,
exited zero and reported a coverage number — a small one, but a number nobody had a
baseline for. It was found by story-0606 only because a campaign reads
`-print_final_stats`, and it would have survived any number of green CI runs.

The cure is `-fsanitize=fuzzer-no-link` on the library: the half that instruments
without claiming `main`. The gate is what keeps it — it reads the archive the targets
link and fails when no SanitizerCoverage symbol is in it, which is a property of the
build rather than a threshold to tune. Measured across the fix: 0 such symbols before,
957 after, and coverage from the *same committed corpora* rose by 5× to 14× per target.

The gate runs in `fuzz-smoke`, before the fuzzers do, because a fuzz run whose feedback
loop is disconnected is worse than no fuzz run: it reports the same green.

## Gate 12: CodeQL reports, it does not gate

**What it asks that nothing else here does.** Every other gate works inside one
translation unit or one input: clang-tidy sees a file at a time, the fuzzers see what
their corpus reaches. Neither follows a length field through three functions into an
allocation size, which for a tool whose entire job is parsing bytes a failing disk or an
attacker chose is the question worth asking. The overflow guards in
`src/core/SafeArith.hpp` are each there because a person noticed.

**What it cannot see yet, which is most of the reason it was worth measuring.** The taint
queries are loaded and they work: a planted, unvalidated length reaching `malloc` was
reported at `high` severity, three times, from a `std::fread`. But **this tree contains no
`fread`.** It reads through `::pread`, `::ReadFile` and `std::ifstream`, and CodeQL's C++
library models flow sources for exactly `fread`, `getdelim`, `gets`, `scanf`, `recv` and
the socket functions — there is no model for any primitive this project actually uses. So
the device read path in `core/io/` is currently *not* a taint source, and no arrangement
of the shipped queries makes it one. This is not a threat-model setting:
`threat-models: [ local ]` was tried against the planted finding and changed the result not
at all, because `fread` is already declared a *remote* source and `pread` is not declared
at all. Closing the gap needs a CodeQL model pack naming this project's own read
primitives; that is recorded as a residual in
[epic-m6](../backlog/epic-m6-loose-ends.md#notes), with the decision it still needs. Until
then gate 12 earns its keep on the rest of the `security-and-quality` suite, and a quiet
run says nothing about the device path.

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
CodeQL finds; it fails only when the build breaks or the database comes back short of it.
GitHub separately attaches a second check named `CodeQL` to a pull request — from the
Advanced Security app rather than from this workflow, so the two sit side by side — and it
can mark *that* one failed when the PR introduces a high-severity alert. Making this a
gate means adding that check to the `protect-main` ruleset, once the first runs have shown
what the signal-to-noise actually is; it gets a story number rather than a quiet settings
change. What is not optional in the meantime is reading it: an alert is fixed, or dismissed
in the Security tab with a stated reason, or it becomes a story.

**What it builds, and when.** Pull requests targeting `main`, a weekly schedule, and manual
dispatch — not every push, because each run pays for a full build of the tree. There is no
`push` trigger on `main`, so the first analysis *of* `main` after a merge comes from a
manual dispatch or the Monday cron, whichever is first. It configures with tests off, so
`src/`, `include/` and `tools/` are analysed and the test suite is not.

**How it refuses to pass vacuously.** A green run over an empty database is
indistinguishable from a green run over a clean one, so the job checks that every file
CMake compiled is present in the database's source archive, by set difference, naming
whatever is missing. Two things that check does *not* do, both deliberate. It does not
compare counts: the archive also holds headers and CMake's own compiler-probe translation
units, so a count can be short by a real file and still look large enough. And it runs
after `analyze`, so it cannot stop a short database from reaching the Security tab — it can
only make the Actions run say so.

## The duplication threshold

Gate 4 fires when a block of **60 tokens or more** is duplicated. Three things
about that are decisions rather than defaults.

**Sixty tokens is one function — measured once per language, not inherited.**
Both medians were measured on 2026-08-06
([story-0703](../backlog/stories/story-0703-gates-measure-python.md)): **61 tokens**
over the 1,517 C++ functions the gate scans, and **63 tokens** over the 198 Python
functions under `tools/`. Both round *down* to 60 — the direction that cannot be
an accommodation — so a block at the bar is a whole typical function's worth of
code living in two places in either language.

**That the two languages landed on the same number is a coincidence of this
tree.** The Python threshold was chosen from the Python measurement; had that
median come out near 40, the gate would carry two numbers. The C++ number is
likewise not converted from the eight *lines* the pre-story-0602 detector used:
lines do not translate into tokens. The C++ median has drifted 62 → 61 since
story-0602 measured it, which is why it is re-measured here rather than quoted —
a number inherited is a number nobody checked.

Reproduce either on any later tree:

```bash
python3 -c "import sys, statistics; sys.path.insert(0,'tools/lint'); import lizard; from source_set import source_files, CPP_SUFFIXES, PYTHON_SUFFIXES; sel = CPP_SUFFIXES;  # or PYTHON_SUFFIXES t=[f.token_count for i in lizard.analyze_files( [str(p) for p in source_files(['src','include','tools'], sel)], exts=lizard.get_extensions([])) for f in i.function_list]; print(len(t), statistics.median(t))"
```

**The threshold is per copy.** `lizard` sizes a clone family by the tokens of
every copy added together, which lets a wide family of short blocks clear a bar
no single copy comes near. Each copy has to reach it here.

**Only code counts — in C++.** A block is reported only when *every one* of its
sites reaches a function body; one site that is all declarations drops the whole
family. Reaching rather than lying inside, because a match runs in windows of
tokens and routinely starts a few lines above the function it is really about.
`lizard` unifies identifiers and keywords alike and collapses literals, so any
two runs of layout constants hash the same, and every byte parser in this tree
opens with an include list, a namespace and a table of on-disk offsets. Those
are different facts wearing the only shape C++ has for stating them, and no
refactoring makes them one. Duplicated *declarations* are the
[self-audit](../code-quality.md)'s business, not this gate's.

**In Python the rule does not apply, and that was settled by experiment rather
than assumed** (story-0703). Python has no preamble: a constant table repeated
in two modules is ordinary refactorable duplication, not the only shape the
language has for stating a fact. Applying the C++ rule to it hid a
153-token-per-copy module-level clone completely, because `lizard`'s function
list for a Python module holds no range covering module scope — so the gate
reported Python *function bodies* while the documentation claimed it covered
Python. A block at module scope is now reported.
`tests/fixtures/duplication/python-module-scope/` is that case, and
`tests/fixtures/duplication/mixed/` still pins the C++ half.

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
