<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0615: CodeQL reads the parsers the way an attacker would

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: In progress
- Size: S

## Goal

Put GitHub's CodeQL analysis on this repository, non-blocking to begin with, so that
untrusted bytes are traced from the device read to the arithmetic they end up in —
across functions and files, which is the one question none of the existing gates ask.

## Design references

- [ADR-0003](../../architecture/adr/adr-0003-validating-carving.md) — precision over
  recall. Every parser in `fs/`, `volume/` and `carve/` reads bytes an attacker or a
  failing disk chose; that is the threat model this analysis is aimed at.
- [quality-gates.md](../../testing/quality-gates.md) — where a new gate is declared, and
  the table saying which job runs which check on which platform.
- [epic-m6](../epic-m6-loose-ends.md#notes) — the note this story replaces: "lands here
  or nowhere before 1.0", with the reasoning for why M5 and M7 are both worse.
- [story-0612](story-0612-ci-runs-gate-targets.md) — a gate that only CI can run is the
  shape that milestone found wrong. This one genuinely cannot run locally, so it is
  declared as CI-only rather than left to look like an oversight.

## What was measured

- **CodeQL is free for this repository**, which it was not before the repository went
  public. No plan, no per-seat cost.
- **The C++ analysis needs a real build.** CodeQL's extractor observes the compiler, so
  the job compiles the tree from scratch. Whatever it costs, it costs on top of the
  existing jobs in billed minutes; in wall clock it runs concurrently with them.
- **CI stands at roughly 13 minutes** and the project has already decided that under 15
  is acceptable ([ci-speed decisions in M5](epic-m5-performance.md)). A parallel job
  changes wall clock only if it is the longest one.
- **The existing gates ask a narrower question.** clang-tidy works a translation unit at
  a time; the fuzzers find what their inputs reach. Neither traces a value from
  `BlockDevice::readAt` through three functions into an allocation size. The overflow
  guards this milestone added — `safeMul64`, `safeAdd64`, `saturatingAdd64` — were each
  put there by a person noticing, not by a machine.

## Design decisions

**Non-blocking first, and gating only on evidence.** The job runs and reports; a finding
does not fail the build. Two reasons. A first CodeQL run over an unanalysed C++ tree
produces a backlog of alerts whose signal-to-noise nobody here has measured yet, and
turning that into red merges on day one is how a gate gets switched off rather than
fixed. And [story-0612](story-0612-ci-runs-gate-targets.md)'s lesson cuts the other way
too: a gate is worth having when someone acts on it, not when it merely exists.
Promoting it to blocking is a follow-up with a number behind it.

**Scheduled plus pull requests to `main`, not every push.** Every push would pay a full
build for a question whose answer changes slowly. A weekly schedule catches drift; a PR
run catches the change that introduced it, which is when it is cheapest to fix.

**The default query suite, unmodified.** `security-and-quality` is what GitHub maintains
and what the alerts are written against. Custom QL is a thing to want *after* the default
suite has been read and found wanting, not before. This survived contact: the one
configuration override that was tried, `threat-models: [ local ]`, provably bought nothing
and was removed again — see below.

**Debug, and warnings not errors.** The analysis needs the compiler observed, not
optimized. `-O0` compiles faster, optimization level does not change what CodeQL extracts,
and the compiler's opinion is already bought four times over in `ci.yml`. An advisory
analysis that goes red because a future GCC gained a warning is one people learn to ignore.
Tests are off, which is also what keeps the job free of vcpkg entirely — so `src/`,
`include/` and `tools/` are analysed and the fixtures are not.

**The job refuses to pass vacuously.** A run over an empty database is green, and so is a
run over a database holding three files because the build stopped early; both look exactly
like "no findings". The job therefore compares CodeQL's own source archive against
`compile_commands.json` and fails when the database is short of what CMake compiled. That
failure is a mechanism failure, not a finding, so it does not make the analysis a gate.

**It is declared CI-only, in the table.** CodeQL cannot run on a developer's machine
without the CLI and a database build, and pretending otherwise is the failure story-0612
was written about. `quality-gates.md` gains a row saying so, with the reason, so the next
person reading that table does not go looking for a local target that was never there.

**What the first run's findings become.** Each alert is either a fix, or a dismissal with
a written reason in the Security tab — never left open and unread. If the first run turns
up something real, it becomes its own story rather than being folded in here: this story
is the analysis arriving, not a promise about what it will say.

## Acceptance criteria

- [x] A CodeQL workflow analyses the C++ tree on pull requests targeting `main` and on a
      weekly schedule, and does not fail a build on findings.
- [x] The run appears in the repository's Security tab with the `security-and-quality`
      suite, over a build that actually compiled the tree — an empty or partial database
      is a failure, not a pass.
- [ ] Every alert from the first run is dismissed with a stated reason or has a story.
      **Open, and cannot close before merge.** All four are triaged above with a written
      verdict and none is a defect, but they were raised against the deleted probe ref;
      the dismissals attach to the first analysis of `main`.
- [x] [quality-gates.md](../../testing/quality-gates.md) records the check, that it is
      CI-only and why, and that it is non-blocking pending a decision to gate.
- [x] `CHANGELOG.md` is untouched: this changes no behaviour an operator can see.
- [x] The epic's CodeQL note is replaced by a link to this story.

## Test plan

There is no unit test for a CI workflow, and inventing one would be the vacuous kind of
check this project has already been bitten by. What stands in for it:

- **The run is watched failing on purpose once.** A branch that introduces an obvious
  taint-flow finding — an unvalidated on-disk length used as an allocation size in a
  scratch file — must produce an alert. Without that, a green CodeQL job proves only that
  the job ran. Delete the branch afterwards; record what the alert said in this story.
- **The database is confirmed non-empty**: the job log states the number of files
  extracted, and it is compared against the tree's translation-unit count.
- The existing suite must be unaffected: no new job may change the outcome of the others.

## What the deliberate run said

The throwaway branch was `probe/0615-taint`, carrying one scratch file. Because `ci.yml`
triggers only on a push to `main` and on pull requests, a temporary `on: push` on that
branch ran CodeQL alone — the check cost one job rather than a whole matrix, and the
branch was deleted afterwards.

**It reported, at `error` severity and `high` security severity,**
`cpp/uncontrolled-allocation-size`, three times:

> This allocation size is derived from user input (string read by `fread`) and could
> allocate arbitrary amounts of memory.

| Shape in the scratch file | Alert |
|---|---|
| `malloc(readLength(image))` — the length crosses a call frame | yes |
| `new char[length]` | yes |
| `malloc(atoi(header))` — the query's own documented shape | yes |
| `memcpy(record, source, readLength(image))` into a fixed 64-byte buffer | **no** |

So the claim in the Goal — a value traced from a read, across functions, into an
allocation size — is one this analysis demonstrably makes, and the reporting path from
query to Security tab works end to end.

**Two things were measured on the way, and both changed the workflow.**

The first attempt at the scratch file reported *nothing*, with a job that was green, a
complete database and the right queries loaded. The size expression was
`malloc(length * sizeof(std::uint32_t))`, and `Bounded.qll` makes an arithmetic expression
that provably cannot overflow a **barrier** — a `std::uint32_t` widened into a 64-bit
multiply cannot. The finding appeared as soon as the length reached the allocator
unarithmetized. That is a real limit on what this gate catches, worth knowing before
anyone reads a quiet run as an all-clear.

The second was a wrong turn worth recording rather than hiding. The silence was first
diagnosed as CodeQL's default `remote` threat model excluding file reads, and
`threat-models: [ local ]` was added to the workflow. The run returned the identical four
results, and `cpp/ql/lib/semmle/code/cpp/models/implementations/Fread.qll` says why:
`fread` is declared a `RemoteFlowSourceFunction`, so the default threat model already
covered it. The override bought nothing and was removed.

**The database was confirmed against the build, not eyeballed.** The job's own step
compares CodeQL's source archive with `compile_commands.json`: `Extracted 213 .cpp files;
the build compiled 211` on the probe branch, and `212` against `210` on the story branch.
The constant difference of two is CMake's own compiler probes — `CMakeCXXCompilerId.cpp`
in the build tree and `CMakeCXXCompilerABI.cpp` under `/usr/share/cmake-*/Modules/` —
which the configure step compiles inside the extractor's window. The suite loaded 179
rules, `cpp/uncontrolled-allocation-size`, `cpp/uncontrolled-arithmetic` and
`cpp/unbounded-write` among them.

## What the first run over the real tree found

Four alerts, all `note` severity, none a defect:

| Alert | Where | Verdict |
|---|---|---|
| `cpp/unused-static-function` ×3 | `Crc32.cpp:19`, `Crc32.cpp:27`, `BuiltinCarvers.cpp:36` | False positive. `tableEntry`, `makeTable` and `flatten` are `constexpr`, consumed only by `constexpr auto kTable = makeTable();` at compile time, so no runtime call site survives for the query to see. |
| `cpp/loop-variable-changed` | `MountTable.cpp:107` | Intentional. `at += escape.has_value() ? kEscapeDigits : 0` consumes a `\NNN` octal escape — three digits plus the loop's own `++at` is the four characters of the escape. |

These were raised against the probe branch's ref and went with it when the branch was
deleted. The dismissals themselves belong to the first analysis of `main`, which is the
run that will hold them.

## Definition of Done

- [ ] Acceptance criteria met — all but the alert dispositions, which attach to the first
      analysis of `main` and so cannot close before merge.
- [x] The deliberate-finding run is recorded in this story, with what CodeQL said.
- [x] Epic row linked and the epic's note replaced.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
