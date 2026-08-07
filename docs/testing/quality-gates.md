<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Quality Gates

These gates run in CI on every push and pull request — except gate 14, which judges a
range and so runs on pull requests only. **Gates 1–11, 13 and 14 must all pass to merge.**
They are the mechanical enforcement of [`AGENTS.md`](../../AGENTS.md). A gate may
only be suppressed inline, at a single site, with a comment justifying it — and blanket
suppressions are rejected in review.

"Must pass" is a project rule, not something the repository enforces on its own: the
`protect-main` ruleset refuses a deletion and a force-push and requires no check to pass,
so a red PR is *merge-able* and merging one is simply not done. Anyone who wants the rule
enforced mechanically adds the checks to that ruleset, which is a change to make on
purpose rather than a thing to assume is already true.

Gate 12 is the exception to blocking, and gate 14 to running everywhere. Gate 12 reports,
a finding does not stop a merge even by
convention, and it runs on a schedule and on pull requests rather than on every push. It
is in the table anyway, because a check nobody wrote down is a check nobody reads. Gate 13
is not an exception: it arrived after gate 12 and blocks like the rest, because a fuzz gate
whose feedback loop is disconnected reports the same green as a working one.

## The gates

| # | Gate | Tool | Fails when… |
|---|------|------|-------------|
| 1 | Formatting | `clang-format --dry-run --Werror` | Any file is not formatted per `.clang-format`. |
| 2 | Static analysis | `clang-tidy` (warnings-as-errors) | Any enabled check fires (naming, size, complexity, bugprone, cppcoreguidelines, …). |
| 3 | File-length gate | `tools/lint/check_file_length.py` | Any C++ **or Python** source file exceeds the file-length limit. |
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
| 14 | ADR immutability | `tools/lint/check_adr_immutability.py` | An `Accepted` ADR's Decision or Consequences changed with no new record naming it superseded. **Pull requests only.** See below. |

## An Accepted ADR is superseded, not edited

Gate 14. [ADR-0001](../architecture/adr/adr-0001-record-architecture-decisions.md)
and the [ADR index](../architecture/adr/README.md) both state the rule and
nothing enforced it — M6 broke it in the same commit range that documented it,
writing the two-tier destination rule into ADR-0005's Consequences in place at
`4a4221e`. That was the M6 audit's highest-severity finding.

**Only Decision and Consequences are frozen.** Status must change; that is how
superseding is recorded. Dates, Context, a corrected link: none is the decision.
Freezing the whole file would make the rule unusable and train people to bypass
the gate, which is worse than not having one.

**Two escapes, and both must name the ADR being edited:** a new ADR in the same
change declaring `**Supersedes:** ADR-NNNN`, or that ADR's own Status becoming
`Superseded`. "Any new ADR in the diff" is deliberately *not* enough — the loose
reading lets a change that legitimately adds one record quietly rewrite an
unrelated one.

**The declaring record must itself be on the record, and must not be the record
it excuses.** A `Proposed` draft naming `**Supersedes:**` is not a successor: it
carries no decision, nothing stops it being withdrawn in the next change — drafts
are freely deletable — and the edit it excused would then have no trace at all.
Requiring the successor to be `Accepted` also brings it under the completeness
check and under the removal refusal, so it must land whole and cannot quietly
disappear. A second file carrying the *same* number is not a successor either.

**A supersession is declared in a `**Supersedes:**` field, not mentioned in
prose.** The field is recognised wherever it appears outside a fenced block —
the header is where it belongs, but the gate does not insist, because refusing a
correctly-declared supersession over its position would teach people to route
around the gate. What it will not accept is a mention. ADR-0012's Context contains the sentence "an
Accepted ADR is superseded by a new record, not edited" two sentences from
"ADR-0005"; reading any nearby mention as a claim excused precisely the edit this
gate refuses, which is how it was first written.

**An example is not a declaration either.** A `**Supersedes:**` inside a fenced
code block is illustration; an ADR documenting the ADR process would otherwise
excuse whatever it named. The clause ends at the next *top-level* bullet or a
blank line, never at a fixed number of characters — a character window swallowed
the following field and excused an edit to the ADR *it* named, while stopping at
any bullet at all refused the natural multi-ADR form, `**Supersedes:**` followed
by an indented list.

**The pre-image decides two things: whether the record was frozen, and which
record it is.** Asked of the post-image, one commit that sets `**Status:**
Proposed` *and* rewrites the Decision passed with exit 0 — a general-purpose
escape hatch reachable by anyone editing the file they were already editing.
There is **no fallback** for a pre-image that will not parse: one existed, and it
reopened the same escape across two commits — mangle the header in the first,
demote and rewrite in the second. Mangling the header is itself the fault now,
which is what makes the second step unreachable.

**The transition escape is granted once, not renewably.** A record on the record
may only move to `Superseded`; going back to `Accepted` is refused. Both statuses
are on the record, so a rule that asked only "is the destination still on the
record" let a record be superseded, rewritten under that excuse, re-accepted, and
superseded again — four commits, a different Decision, and no successor anywhere
in the tree.

**A declaration may not be withdrawn once it has been spent.** The
`**Supersedes:**` field is a header bullet and so sits outside both frozen
sections: a successor could be admitted, excuse a rewrite, and then have its
clause tidied away in a change touching no frozen line, leaving the rewritten
Decision with nothing pointing at it. The set of numbers a record declares may
grow but not shrink.

That refuses one legitimate change too, deliberately: **correcting a number
declared by mistake.** The gate cannot tell whether the declaration has already
excused an edit without reading further back than the range it was given, which
is the diff archaeology this design rejected at the outset. So the remedy is the
ordinary one — correct it before the record lands, where there is no pre-image to
compare against, or write a new record saying what the old one got wrong — the same
remedy the removal rule gives, and for the same reason: both doors open forwards
only. The general form is the lesson the five fixes before it all
missed — **an escape's evidence must be as durable as the thing it excuses**;
they each asked what a *previous* change could manufacture, none what a *later*
one could destroy.

**A `Superseded` record is frozen too, and the escape is the *transition*.** The
edit is excused by the change in which the Status *becomes* `Superseded`, not by
the Status being `Superseded`. Those differ from the next change onward: read as
a property of the file as it stands, the post-image goes on saying `Superseded`
forever, and every later rewrite of that record would be excused by it. A
superseded ADR is still the record of a decision that was taken — surviving to be
read is the whole reason for superseding rather than deleting. Only a draft that
was never accepted may still be reshaped, or withdrawn.

**A record leaves `Accepted` only by becoming `Superseded`.** The Status line is
not frozen and must not be — that is how a supersession is recorded — but a
change that sets `Proposed` takes the record off the record, and the next change
then finds a pre-image that is not frozen and may rewrite the decision freely.
Two green pull requests, the second needing no cleverness at all. The demotion is
itself the breach, refused in the change that makes it.

**Removing a record on the record fails, and no supersession excuses it —
including one from an earlier change.** Supersession excuses an *edit* because
the superseded record survives to be read; that is the whole mechanism, so a rule
protecting only `Accepted` would have left the two-step version wide open: mark
it `Superseded` in one pull request, which passes and must, then delete it in the
next, whose pre-image now reads `Superseded`. A removal destroys the record, so a
successor alongside makes the loss no smaller: mark it `Superseded` and leave it
where it is. Removal means deleted,
moved off the naming convention (into a subdirectory, to `.markdown`, under a new
prefix), **or renumbered** — git reports all of those as renames, so a delete
filter saw only the first, and a renumbering touches no line of prose at all
while retiring the number every citation uses. A **rename** is judged as an edit
of the same record only while it stays the same record: correcting a slug passes,
rewriting behind one does not, and changing `adr-0005-` to `adr-0099-` is a
removal however little the text moved.

**No range may leave a malformed `Accepted` ADR behind it.** Everything else here
is strict about the file as it now stands and forgiving about the pre-image, which
only works if a malformed record can never enter: nothing parsed the files a range
left in place, so one arriving with an unclosed fence made every later range over
it exit 2 — including the commit that would have repaired it. The check is on
*arrival*, not on addition, because those are different moments: a `Proposed` ADR
is a draft and may be as incomplete as it likes, so landing a draft and then
promoting it walked an incomplete record past a check that only looked at added
files. A status change is an arrival, and so is a rename into the convention.

**The gate reads git with the flags that decide what it sees, and runs from the
top level.** Four things each made it exit 0 on the historical breach it exists to
catch, and none of them needed anything exotic:

| What | Answered by |
|------|-------------|
| Running it from any directory but the root — the pathspec is relative, so it matched nothing while the range stayed non-empty | every command runs from `git rev-parse --show-toplevel` |
| One committed `.gitattributes` line marking the ADRs `-diff`, after which git prints "Binary files differ" and no hunk headers at all | `--text` |
| `diff.external` in the reader's config, which replaces the patch wholesale | `--no-ext-diff` |
| A `textconv` filter, which renumbers every line the hunk headers name | `--no-textconv` |

Flags, not `-c` pins: a command-line flag outranks configuration. Each flag above
has one test that fails without it — and a test that first proves its own fixture
really does derange git, since a driver the flags stop git from ever invoking
would prove nothing either way.

**The gate refuses when no ADR exists at the range's end**, asked last and only
of an otherwise-clean run. "No ADR changed" and "there are no ADRs
to change" are indistinguishable from outside, and the second means this gate is
covering an empty set — story-0704's rule, applied to the one gate that reads a
range instead of a tree and is therefore exempt from the meta-test enforcing it.
Because gate 14 runs on pull requests only, the change that relocated the
directory would otherwise reach `main` without ever meeting the gate it silenced.

**Anything the gate cannot read is a fault — exit 2, never a pass.** An
unparseable `**Status:**`; a code fence opened and never closed; an Accepted ADR
with no Decision or Consequences heading, or with either of them *twice*, since
only the first would own a span and everything under the second would be
unguarded; a range naming one commit rather than two, which `git diff` would
silently answer from the working tree. Each was a silent pass first, and each
disabled the gate for that file permanently while it lasted: one character —
`- Status:` for `- **Status:**` — was enough.

That includes the faults that are not about ADRs at all: **no `git` on the path,
and any byte in an ADR that is not UTF-8** (`docs/` is outside the encoding
gate's roots, so nothing else catches it first). Both escaped as a traceback and
exit **1** — the code this document reserves for "found a violation" — so a gate
that could not read its input reported a breach that was not there.

**What it cannot catch**, because a gate that overstates itself is this
milestone's subject: an *inaccuracy*. An ADR that was wrong the day it was
written passes forever, and the ADR-versus-code observations M7 left open —
listed in [epic-m7-hardening](../backlog/epic-m7-hardening.md) — would sail
through, since none of those ADRs was ever edited. Accuracy is the milestone audit's job.

**Pull requests only, and the range is supplied rather than guessed.** A gate
that infers its own input is how you get one that inspects nothing. CI passes
`base.sha...<the merge commit>`. Three dots measures the *old* side from the merge base, which
is what `git diff` does with them and what a run by hand against a moving `main`
needs; on a pull-request event the right-hand side is already the merge commit,
so the two spellings coincide there and the separator costs nothing.

**A range the diff reads nothing from exits 2**, and "nothing" is counted the way
the diff counts it. `rev-list --count a...b` counts the symmetric difference
while `git diff a...b` reads `merge-base(a,b)..b`; they disagree exactly when the
guard matters, so a reversed or stale range expression passed while inspecting an
empty diff — this gate's own subject, in its own vacuity guard.

## A gate that inspected nothing fails

Every walking gate refuses two things before it reports anything: a root that
does not exist, and a root that matched no files — **judged per root, not over
their union**. A gate handed `src include tools` where `include` holds nothing is
covering less than it claims, and a non-empty union hides that completely. The
refusal names the gate and the root. Both live in
`tools/lint/source_set.py`: `gate_files` resolves the roots, naming a missing one
where it finds it and an empty match through `report_empty_gate`, so a gate
inherits both by resolving its file set through the shared discovery rather than
by remembering to check (story-0704). `refuse_empty_gate` is the predicate form
of the same message, for the two `run_gate` functions that are handed a file list
rather than roots. Both exit **2**: the gate could not run, as distinct from **1**,
which is a violation it found.

This is a mechanism rather than a convention because the convention had already
failed. The rule was spelled out in four walking gates, and `check_file_length.py`
— which enforces [AGENTS.md §2](../../AGENTS.md#2-hard-limits-enforced-by-clang-tidy--ci-scripts)'s
headline number — had neither the guard nor a unit test.

`tests/unit/lint/test_gate_vacuity.py` holds it: **no module under `tools/lint/`
except `source_set` may call a file-discovery function**, and every gate that
reaches `gate_files` must exit non-zero, naming the root, over a directory that
exists and holds nothing. The detection is over the AST, so prose in a docstring
is not a finding and `scandir`/`iterdir`/`iglob` are.

**Two gates are exempt, and are identified by not reaching `gate_files` rather
than by name.** `check_coverage.py` refuses when the coverage export counted no
core *line*, and `check_fuzz_instrumentation.py` when it was handed no archive —
the same principle over inputs that are not file sets. Both return **1** rather
than 2, which predates this rule and is not reconciled with it.

**Per-root refusal has a cost, and it is concrete.** Every C++ file under
`tools/` lives in `tools/imagegen/`; the rest of `tools/` is Python. So that one
directory is what keeps `format` and `layer` — both C++-only — non-empty over the
`tools` root. Before this rule the union with `src` and `include` covered it.
Moving or emptying `tools/imagegen/` now turns two gates red, and the fix is the
next paragraph rather than a flag.

**A root that deliberately contributes nothing to a gate is removed from that
gate's root list**, not exempted here. There is no flag for "expected to be
empty": a gate pointed at a directory it will never find anything in is a
configuration statement, and the configuration is where it belongs.

**The boundary this does not cover:** `source_files` remains public, and a caller
that uses it directly gets no refusal. `tools/lint/median_function_tokens.py` is
the one such caller in the tree, and it carries its own — a median over no
functions is not a measurement. Nothing mechanical stops a future caller from
choosing that route and forgetting.

## Which gates read which language

The five gates that walk the tree share their file discovery
(`tools/lint/source_set.py`), and **each states the suffixes it can analyse**
rather than all reading one global set (story-0703):

| Gate | C++ | Python | Why |
|------|:---:|:------:|-----|
| File-length gate | yes | yes | AGENTS.md §2 states the limit without naming a language, and "too many responsibilities" is language-independent. |
| Duplication | yes | yes | `lizard` tokenizes both; duplicated *knowledge* is the target. |
| Source encoding | yes | yes | A stray non-UTF-8 byte is a defect in any text file. |
| Formatting | yes | **no** | It runs `clang-format`, which does not format Python. A Python formatter would be a new gate and a separate decision. |
| Layer direction | yes | **no** | The layer DAG is a statement about C++ includes; `import` is not one. |

The two "no" rows are why the suffix set is per gate. Widening one shared
constant — the obvious change — would have handed Python to `clang-format` and
to the include-DAG parser, which is how a gate starts reporting on something it
cannot analyse.

Byte-compiled files are never walked, because `.pyc` is in no suffix set — so no
gate can fail on a file nobody can fix in the source. There is deliberately no
`__pycache__` guard: one was written, and removed again when it turned out to
defend against a case Python cannot produce (story-0703).

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
| 3 | File-length gate | `guards` + `build-test` (windows) — the `guard-limits` **target** | yes | yes |
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
| 14 | ADR immutability | `guards`, **pull requests only** — a push to `main` has already merged | yes | **no** |

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
Measured at story-0703's head: **61 tokens** over the 1,517 C++ functions the gate
scans, and **64 tokens** over the 201 Python functions under `tools/`. Both round
*down* to 60 — the direction that cannot be an accommodation — so a block at the
bar is a whole typical function's worth of code in two places, in either language.

**That the two landed on the same threshold is a coincidence of this tree.** The
Python number was chosen from the Python measurement; had that median come out
near 40, the gate would carry two thresholds. The C++ median has drifted 62 → 61
since story-0602 measured it, which is exactly why it is re-measured here rather
than quoted: a number inherited is a number nobody checked.

Reproduce either on any later tree — and the numbers above will move, because the
script counts itself among the Python files:

```bash
python3 tools/lint/median_function_tokens.py cpp
python3 tools/lint/median_function_tokens.py python
```

This is a script rather than a one-liner in this page because the one-liner that
preceded it **computed nothing and exited 0** — an inline `#` swallowed the rest
of the line. A command that proves a number is checkable must itself be run
before it is quoted.

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
