<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0612: CI runs the real gate targets on both platforms — a gate that dies quietly turns red

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: Ready
- Size: S

## Goal

`format-check` and `guard-limits` are the commands every contributor is told to run,
and CI runs neither of them. It runs a bash rewrite of one and a bare call to the
other's script, on Linux only, while the Windows leg builds and tests and checks
nothing. Point both jobs at the literal CMake targets, on both platforms, so the
verdict a developer gets locally and the verdict that gates a merge come from the
same code — and so a gate that stops working fails the run instead of passing it.

## Design references

- [`.github/workflows/ci.yml`](../../../.github/workflows/ci.yml) — the two jobs this
  story edits. The `guards` job reimplements formatting in bash (`:43-51`) and calls
  the length guard's script directly (`:41-42`); the `windows-latest` leg of
  `build-test` runs configure, build and test and nothing else (`:79-87`).
- [`cmake/DevTargets.cmake`](../../../cmake/DevTargets.cmake) — the targets CI will
  invoke. Both exist only if `find_program` found their tools at configure time
  (`:19-21`, `:26`, `:107`), which fixes the step ordering this story needs.
- [story-0607](story-0607-format-gate-argument-list.md) — **the enabler.** Until
  `84c0774` the Windows `format-check` target could not be invoked at all: it died in
  `CreateProcess` before clang-format started. Asking CI to run it would have been
  asking CI to run a corpse. The batching driver
  ([`tools/lint/check_format.py`](../../../tools/lint/check_format.py)) is what makes
  the target a thing a runner can call, and
  [`tools/lint/source_set.py`](../../../tools/lint/source_set.py) is what makes "which
  files do the gates cover" have one answer — which this story then stops CI from
  contradicting.
- [story-0602](story-0602-python-duplication-gate.md) — owns the one gate this story
  leaves alone, and for a concrete reason stated below.
- [epic-m6](../epic-m6-loose-ends.md) outcome line `:25`, and the M5 audit's
  [automation finding](../epic-m5-performance.md#milestone-architecture-audit)
  (`:160-167`) — the three-instance failure class this story is the answer to.
- [quality-gates.md](../../testing/quality-gates.md) — the gate table, and the
  "Changing a gate" rule (`:72-76`) this story is bound by, being one.

## What was measured

**The verdict layer failed three times in one increment** — not the code under test,
the machinery that judges it. `tidy` reported green off stale stamps until a hook
started invalidating them on header edits (`.claude/hooks/invalidate_tidy_stamps.py`,
landed in `48ba94b`). The perf gate called a regression on story-0503's own PR that
was the host's memory bandwidth: 6.9× slower while executing *16% fewer*
instructions, fixed in the same PR by the corroboration rule (`d8867a1`;
`tools/perf/compare_baseline.py:12-16`). And `format-check` was dead on every Windows
invocation from the moment the tree crossed 32,767 characters — through the v0.3.1
release (`996e31b`) and out the far side — until story-0607 (`84c0774`). Two of the
three failed *green*. That is the class: the gate does not tell you it stopped
working, because a gate that has stopped working has also stopped complaining.

**CI invokes `format-check` and `guard-limits` on no platform.** The `guards` job
runs a `shopt -s globstar` file list piped into clang-format (`ci.yml:43-51`) and
`python3 tools/lint/check_file_length.py` directly (`:41-42`). The `windows-latest`
matrix leg (`:63`) has no lint step at all: configure (`:79-81`), build (`:82-84`),
ctest (`:85-87`).

**The audit's phrasing is one target too broad, and the exception is the proof.** The
M5 finding says CI "invokes the real gate targets on no platform"; `tidy` is invoked
as a real target, on ubuntu, at `ci.yml:135-139` — and it is the gate that carries a
fail-fast against exactly this story's failure mode, because a shard index matching
nothing would leave the target with no work and *pass*, so it stops the configure
instead (`DevTargets.cmake:72-81`). The claim holds precisely for the two targets
this story is about. The pattern it is asking for already works here.

**The bash rewrite agrees with `source_set.py` today, and nothing holds it there.**
`src/**/*.{cpp,hpp} include/**/*.hpp tests/**/*.{cpp,hpp} tools/**/*.{cpp,hpp}`
selects the same files as `source_files(['src','include','tests','tools'])` at
`.cpp`/`.hpp` (`source_set.py:17-27`) on the current tree. Add one `.cpp` under
`include/` and the target checks it while CI does not. The file-length call matches
its target's arguments exactly (`ci.yml:42` against `DevTargets.cmake:110-111`) —
also by hand, also by luck.

**A configure that resolves nothing is cheap, and this tree permits one.** The only
`find_package` in the repository is `GTest` (`tests/CMakeLists.txt:2`), reached only
when `REVENANT_BUILD_TESTS` is ON (`CMakeLists.txt:73`). So
`cmake -S . -B build/gates -DREVENANT_BUILD_TESTS=OFF` needs no vcpkg, no ninja and
no dependency resolution — a compiler probe for `project(… LANGUAGES CXX)` and a
glob. `revenant_add_dev_targets()` runs unconditionally (`CMakeLists.txt:84-85`) and
the roots it hands the driver are fixed (`DevTargets.cmake:30-32`), so turning tests
off does not shrink what the format gate covers.

**Every other job waits on `guards`** (`ci.yml:59`, `:101`, `:148`, `:183`, `:267`).
A minute spent there is a minute on the whole run, which is why the paragraph above
matters.

## Design decisions

**The Windows leg runs the targets, after Configure and before Build.** One step,
two commands: `cmake --build --preset debug --target format-check` and
`… --target guard-limits`. They need a configured build directory and nothing that
was compiled in it, so they have no business queueing behind the slowest leg in the
matrix. The alternative — after the Test step, so a formatting nit cannot cost the
Windows build signal — was rejected: `build-test` declares `needs: guards`, so the
same two verdicts on the same files already passed on Linux before this job starts.
Anything that fails here is a mechanism failure, and a mechanism failure is what
this story exists to surface, loudly and early.

**Installation precedes configure, because `find_program` runs once.**
`REVENANT_CLANG_FORMAT` and `REVENANT_PYTHON` are resolved at configure time and
cached (`DevTargets.cmake:19-21`); a clang-format installed after `cmake --preset
debug` is a clang-format the targets do not know about, and the format targets
simply would not exist (`:26`). The Windows leg gains, before its Configure step:
`actions/setup-python` (SHA-pinned like every other action in the file) so one
deterministic interpreter satisfies both the wheel install and `find_program`, and
`python -m pip install clang-format==${CLANG_TOOLS_VERSION}` — the same PyPI wheel,
at the same pin, for the same reason the `guards` job gives at `ci.yml:33-40`. It
adds nothing else: CMake arrives with `lukka/get-cmake` (`:72`) and the MSVC
environment with `msvc-dev-cmd` (`:69-70`).

**The configure is told which clang-format to use.** `windows-latest` ships its own
LLVM, whose version follows the image and not our pin, and PATH order between a
runner image's toolchain and a pip Scripts directory is not a thing to bet a merge
gate on. So `-DREVENANT_CLANG_FORMAT=<the pinned binary>` goes on the Configure line;
`find_program` honors a cache entry that is already set. The apt-18-versus-local-22
flap the `guards` job documents is the same hazard with a different distributor.

**The version is named once.** `22.1.8` appears in the workflow as a job-independent
`env:` entry beside `VCPKG_COMMIT` (`ci.yml:21-23`), referenced by the `guards`
install step and the new Windows one. Two hard-coded copies of a pin drift; the
`tidy` job's `clang-tidy==22.1.8` (`:130-132`) is the same number for the same reason
and may join it, which is a rename, not a scope increase.

**The `guards` job gains a configure and loses its rewrite.** It gains one step —
`cmake -S . -B build/gates -DREVENANT_BUILD_TESTS=OFF`, the dependency-free configure
measured above, using the image's own cmake and g++ the way the `tidy` job uses the
image's — and then invokes the same two targets against that directory. It drops the
globstar block (`ci.yml:43-51`) and the direct script call (`:41-42`) entirely. After
this, there is no second implementation of a gate anywhere in the repository, and
`quality-gates.md`'s local pre-flight block is not a mirror of CI; it is CI.

**Nothing is tolerated.** No `continue-on-error`, no `|| true`, no `if:` that can
skip a gate step on a tree that has sources. A missing tool means a missing target
means a red step, which is the correct and desired outcome; `DevTargets.cmake` keeps
its permissive `if(REVENANT_CLANG_FORMAT AND …)` guard so a contributor without
clang-format can still configure locally, and CI's job is to be the machine where
that guard is never satisfied quietly.

**The duplication detector now has a target to invoke.** story-0602 replaced
jscpd with `tools/lint/check_duplication.py` and gave it the `duplication`
target, so `DevTargets.cmake` defines `format`, `format-check`, `tidy`,
`guard-limits` and `duplication`, and the `guards` job's duplication step is a
hand-written command line like the two this story is already replacing. It joins
them rather than staying an exception.

**`tidy` on Windows is out of scope, and the epic's line is amended rather than
over-claimed.** clang-tidy cannot parse the MSVC ASan + `/MDd` combination, so the
Windows procedure runs it from the `release` preset (`quality-gates.md:50-53`) — a
second configure and an entire extra optimized Windows build, which the budget will
not have and which belongs with the audit's *other* gate row (the release build
compiling tests and an optimized clang leg), not here. So: `epic-m6-loose-ends.md:25`
becomes machine-checked for `format-check` and `guard-limits` on both platforms, and
this story says out loud which remainder stays prose — Windows `tidy`. An outcome
line that claims more than the machine checks is how we got here.

**The budget is unmoved.** What is added: on `guards`, a configure that resolves no
dependency and compiles nothing, plus two Python processes over the tree; on Windows,
a pip install and the same two Python processes into a build directory that is
already configured. The run's long poles — the Windows build-and-test leg and the
four `tidy` shards — are untouched, and the 15-minute ceiling
(`epic-m6-loose-ends.md:95`) is not in play. The story records the observed delta
from the run's own timings rather than asserting it.

## Acceptance criteria

- [ ] The `windows-latest` leg of `build-test` invokes `cmake --build --preset debug
      --target format-check` and `--target guard-limits` literally, after Configure,
      and both reach a verdict visible in the run log.
- [ ] The `guards` job invokes the same two targets against a configured build
      directory; `ci.yml` contains no `clang-format` invocation and no
      `check_file_length.py` invocation outside them.
- [ ] The globstar file list and the direct script call (`ci.yml:47-57`) are deleted,
      not commented out or kept "for comparison".
- [ ] A gate that cannot run fails the run: no `continue-on-error`, no `|| true`, and
      a step whose target does not exist is a red step. Demonstrated, not asserted.
- [ ] The clang-format the Windows leg runs is the pinned version, printed in the log
      (`clang-format --version`) rather than assumed, and the version string appears
      once in `ci.yml`.
- [ ] A misformatted file turns both the Windows leg and the `guards` job red naming
      the file; a file over 250 lines turns both red naming the file; a clean tree is
      green. All four recorded in this story with run links.
- [ ] [quality-gates.md](../../testing/quality-gates.md) states, per gate, which job
      runs it and on which platforms — including the two that remain Linux-only and
      why.
- [ ] The duplication step invokes the `duplication` target too, so no gate in
      `guards` is still a hand-written command line.
- [ ] The wall-clock the change added to each job is recorded here, from CI's own
      timings.

## Test plan

**The run is the test.** This story ships no C++ and adds no unit seam; a workflow
file is only true on a runner, and the PR's own CI on both platforms is the evidence.
What the story must not do is claim the verdict works without having watched it come
out red.

**Two deliberate red paths, on separate pushes so neither masks the other.** First, a
correctly formatted file padded past 250 lines: `guard-limits` fails on Windows and on
Linux naming it, `format-check` stays green. Then the same file misformatted
(story-0607's `int   main( ){return 0;}` is the known-good provocation):
`format-check` fails on both, naming the file. Both are reverted and the run goes
green. The Windows failure must be a *formatting* verdict, not a launcher error —
that distinction is the whole story, and it is checked by reading the failure text,
not the exit code.

**Already covered, deliberately not duplicated.** The gate scripts' own behavior —
discovery, batching, the verdict, the refusal to pass an empty set — is unit-tested
under `LintUnitTests` (`tests/CMakeLists.txt:260-262`), and the missing-root refusal
runs end to end as `FormatGateRefusesAMissingRoot` (`:165-169`), on both platforms,
inside the ctest step this job already runs. This story adds the target-level half of
the same guarantee, not a second copy of it.

**Reviewed, not measured:** that a *future* mechanism failure is caught. Nothing can
test the failure that has not been invented yet; what makes the claim true is
structural — CI invokes the literal target, with no fallback path and no tolerance
flag — and a reviewer checks that property by reading the workflow, the same way
story-0607's "no fixed-size command line anywhere in the mechanism" was reviewed
rather than measured.

## Definition of Done

- [ ] Acceptance criteria met, tests green under ASan + UBSan.
- [ ] clang-format, clang-tidy, duplication and file-length guard clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md))
      completed.
