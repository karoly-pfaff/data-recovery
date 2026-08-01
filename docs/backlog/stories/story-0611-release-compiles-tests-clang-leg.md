<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0611: The release build compiles the tests, and clang gets an optimized leg

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: In progress
- Size: S

## Goal

CI has one optimized build, and it deliberately compiles no test code; it has three
clang builds, and every one of them is Debug. Close both holes: let the release preset
build what it says it builds, and add an optimized clang leg — so that
the warning contract means what it claims
in every configuration this project ships from, not only in the one it debugs in.
([AGENTS.md](../../../AGENTS.md) §6.3 requires a warning-free build on both platforms
with warnings-as-errors; the flags themselves are owned by
[quality-gates.md](../../testing/quality-gates.md) gate 5, which names `/W4 /WX` for
MSVC alongside the GCC and Clang form. story-0614 moved them there.)

## Design references

- [`.github/workflows/ci.yml`](../../../.github/workflows/ci.yml) — line 193 configures
  the release job with `-DREVENANT_BUILD_TESTS=OFF`; lines 178–181 explain why; lines
  195–208 stage and publish `release-binaries`; line 217 is `benchmarks`' `needs:`.
- [`CMakePresets.json`](../../../CMakePresets.json) — line 36: the release preset sets
  `REVENANT_BUILD_TESTS` **ON** itself. Lines 75–78 define a `release` *test* preset
  that nothing in CI has ever been able to run.
- [`CHANGELOG.md`](../../../CHANGELOG.md) — lines 238–247 and 749–752: the three
  instances of the bug class this story retires.
- [`cmake/CompilerWarnings.cmake`](../../../cmake/CompilerWarnings.cmake) — line 27
  (`-Wnull-dereference`) and lines 39–43 (the GCC-only set): warnings whose usefulness
  depends on an optimizer that CI runs over two thirds of the tree.
- [`tests/CMakeLists.txt`](../../../tests/CMakeLists.txt) — line 133: `revenant_tests`
  links `revenant_project_options`, so a test TU compiles under the full warning
  contract wherever it is built.
- [epic-m5](../epic-m5-performance.md#notes) lines 116–119 — "CI must not grow another
  C++ build". Reconciled head-on below.
- [story-0607](story-0607-format-gate-argument-list.md) — the neighbouring gate story:
  what a gate story records as measured, and what it records as reviewed.
- [quality-gates.md](../../testing/quality-gates.md) — gate 5 ("any compiler warning on
  MSVC, GCC, or Clang", line 18) is the promise; lines 72–76 are the procedure a gate
  change follows.

## What was measured

**The bug class, three instances, all on record.**
[`CHANGELOG.md:238-247`](../../../CHANGELOG.md) records two latent bugs that the
first-ever GCC `-O2` build found the first time it ran: `Result::map` and
`Result::andThen` branched on the pointer `std::get_if` returns instead of on
`hasValue()`, leaving an unguarded dereference under `-Wnull-dereference`; and GPT's
`withoutChecksum` zeroed four bytes at a fixed offset into a copy whose length it had
not checked, which `-Warray-bounds` reported and which was undefined behaviour for a
short header. Both fixes are in commit `c2e8da0` — the same commit that introduced the
`build-release` job, which is the point: the job found them by existing.
[`CHANGELOG.md:749-752`](../../../CHANGELOG.md) is the third, from 0.1.0 — partial
designated initializers that MSVC accepts and clang rejects, caught by a clang build
done by hand. (The M5 audit cited these at 213–222 and 724–727; story-0607's
`[Unreleased]` entry has since pushed both down 25 lines.)

**The existing check is partial.** [`ci.yml:193`](../../../.github/workflows/ci.yml)
overrides the release preset's own `REVENANT_BUILD_TESTS: ON`
([`CMakePresets.json:36`](../../../CMakePresets.json)) back to `OFF`, so **no test
translation unit is compiled at `-O2 -Werror` anywhere in CI**. What each leg actually
covers, as configured today:

| Job | Compiler | Configuration | `src`/`tools` | test TUs |
|-----|----------|---------------|:---:|:---:|
| `build-test` (ubuntu) | GCC | Debug, ASan+UBSan | yes | yes |
| `build-test` (windows) | MSVC | Debug, ASan+UBSan | yes | yes |
| `coverage` | clang | Debug + instrumentation | yes | yes |
| `fuzz-smoke` | clang | Debug + libFuzzer | yes | yes |
| `tidy` ×4 | clang-tidy | analysis only, Debug DB | — | — |
| `build-release` | GCC | **RelWithDebInfo** | yes | **no** |

Two gaps fall out of that table. The optimized column has one entry and it is missing
its test TUs; and **no optimized clang build exists at all** — every clang leg is
Debug, so clang's codegen has never seen this tree at `-O2`.

**The comment says so out loud.** [`ci.yml:180-181`](../../../.github/workflows/ci.yml):
"Tests are off: this build exists to be *run*, and the debug leg already ran them." The
reasoning is that an optimized build's only product is its binaries. The 0.3.0 `Fixed`
entry above is that reasoning disproved by the very job the comment is attached to: the
build's other product is the compiler's opinion, and the debug leg cannot supply it.

**Building the tests optimized applies the whole contract.**
[`tests/CMakeLists.txt:133`](../../../tests/CMakeLists.txt) and
[`tests/fuzz/CMakeLists.txt:6`](../../../tests/fuzz/CMakeLists.txt) both link
`revenant_project_options`, so every warning in `CompilerWarnings.cmake` and `-Werror`
apply to test code exactly as to `src/`. Scope note: the release preset leaves
`REVENANT_BUILD_FUZZERS` at its `OFF` default
([`CMakeLists.txt:24`](../../../CMakeLists.txt)), so this story reaches `revenant_tests`
and not the fuzz targets.

**The artifact does not change.** `REVENANT_BUILD_TESTS` gates exactly one thing —
`add_subdirectory(tests)` at [`CMakeLists.txt:73-76`](../../../CMakeLists.txt). No flag,
define or link line of `revenant`, `revenant_cli`, the two frontends or `imagegen`
depends on it, and the staging step copies four fixed paths
([`ci.yml:199-203`](../../../.github/workflows/ci.yml)). `gtest` is an unconditional
vcpkg dependency ([`vcpkg.json:8-10`](../../../vcpkg.json)), so the release job already
pays to install it and today gets nothing for the money. Consumers are `benchmarks`
(`needs: build-release`, downloads `release-binaries`) and, per
[epic-m7:35](../epic-m7-release.md), story-0701's packaging. Both keep consuming the
identical artifact; only what else the job compiles changes.

**The budget has room, and the change is not on the critical path.** Run
`30540830546` on `main` (2026-07-30, `5c0a974`), the last green full run before this
story: whole run **10m11s** against the 15-minute budget. `Build & test
(windows-latest)` is the critical path at **9m41s**. `Release build (artifact)` takes
**1m35s** and `Benchmarks` **50s**, so that whole chain finishes at T+2m59s — nearly
seven minutes inside the Windows leg's shadow. For scale, the ubuntu debug job compiles
the same tree *plus* the tests *and* runs ctest in 4m59s.

**Two objections checked and dead.** `RelWithDebInfo` defines `NDEBUG`: there is no
`assert(` anywhere in `src/`, `include/`, `tests/` or `tools/` (the only matches are
two comments), so nothing changes behaviour. And clang's *frontend* diagnostics over
test TUs are already covered — the `coverage` job's preset inherits `debug`
([`CMakePresets.json:44`](../../../CMakePresets.json)), so tests are ON and
warnings-as-errors is ON with `CC=clang`. The clang leg buys the optimized delta and
nothing else. That is a smaller prize than the GCC half, and this story says so rather
than overselling it.

## Design decisions

**Drop the override; the preset is the definition.** The fix to half (a) is deleting
`-DREVENANT_BUILD_TESTS=OFF` from `ci.yml:193`. A preset that declares tests ON and a
CI job that quietly declares them off is a configuration with two answers, and the
answer that shipped was the weaker one. After this, `cmake --preset release` means the
same thing on a developer's machine and on the runner.

**The clang leg is the same job with a second compiler, not a new job.**
`build-release` becomes a two-entry matrix — `{cc: gcc, artifact: true}` and
`{cc: clang, artifact: false}` — keeping the job *key* `build-release`, because
`benchmarks`' `needs:` and M7's packaging both name it. One recipe compiled twice
cannot drift; two job definitions with copied steps will. The staging and upload steps
carry `if: matrix.artifact`, so exactly one leg publishes, and `fail-fast: false`
matches the other matrices here so a clang failure does not cancel the artifact leg.
The clang leg installs clang the way the `tidy` job does (`ci.yml:131`) rather than
trusting whatever the runner image ships.

**Reconciling "CI must not grow another C++ build"
([epic-m5:116-119](../epic-m5-performance.md#notes)) — the clang leg breaks its letter,
on purpose.** That note protects two different things and they have different answers
here. What it actually forbids is a *consumer* building its own tree instead of taking
the artifact, and this story does not touch that: `benchmarks` and M7 still consume one
artifact from one build. What it also counts is total compiles, and by that count this
is +1 (ten builds, not nine), because there is no way to compile clang-optimized
without compiling clang-optimized. The accounting is: **+1 build, ≈4 runner-minutes,
+0 wall clock** — the leg starts at T+0 alongside the others and lands inside a
9m41s critical path it does not touch. The note's own premise was a performance
milestone's runner-minute bill on an account that had exhausted its free minutes;
[the repository is public now](../epic-m5-performance.md#notes), which returned them.
And the thing being bought is a bug class with three recorded instances, two of which
appeared the first time an untried configuration was built, retired **before** M8 adds
parsers rather than after. If that trade is refused, the fallback is stated in advance
and is not a compromise on half (a): drop the clang leg, keep the override deletion,
and the two GCC instances' configuration is covered.

**Compile, do not run.** Neither leg runs `ctest`. The debug legs run the suite under
ASan+UBSan, which is the stronger dynamic check; an unsanitized optimized run would
cost wall clock for a weaker signal. What is bought here is the compiler's opinion, and
it is delivered by compiling. (`ctest --preset release` becomes runnable locally as a
side effect. Wiring it into CI is a different story, and this one is not it.)

**The leg proves it compiled something.** A compile gate that compiles nothing passes,
which is how `-DREVENANT_BUILD_TESTS=OFF` went unnoticed for a milestone. So the job
asserts `build/release/tests/revenant_tests` exists after the build and fails if it
does not — the same refusal-to-pass-vacuously as the tidy shard-index check
([`DevTargets.cmake:68-81`](../../../cmake/DevTargets.cmake)), the coverage gate's
empty-match test ([`tests/CMakeLists.txt:156-159`](../../../tests/CMakeLists.txt)), and
story-0607's empty-set refusal. It is also what stops the override quietly returning.

**Whatever the first run says gets fixed or recorded, never silenced.** This is
story-0602's rule about a new gate's day-one findings, applied to a compiler: no
`-Wno-` flag is added, no warning is dropped from `CompilerWarnings.cmake`, and no
`REVENANT_WARNINGS_AS_ERRORS=OFF` appears for tests. Either a diagnostic is a real
defect and is fixed, or it is explained per site in this story.

**Gate bookkeeping.** [quality-gates.md:72-76](../../testing/quality-gates.md) requires
a gate change to move `AGENTS.md`, that file and the config together. Gate 5's row
promises "any compiler warning on MSVC, GCC, or Clang" and has been true only at `-O0`;
it now names the configurations too, so the next reader can see the shape of the
guarantee without reading `ci.yml`.

## What the first run found

Measured 2026-08-01 on the WSL bench, which reproduces the release preset's cache
variables (`RelWithDebInfo`, tests ON, sanitizers OFF, warnings-as-errors ON) with
`-k 0` so one pass collects every diagnostic rather than the first.

**GCC 14.2.0: five `-Wnull-dereference` instances, two translation units, both in test
code that had never been compiled optimized anywhere.** Clang 19.1.7 on the same tree:
clean, 126 test objects, binary produced — so the clang leg buys the optimized delta and
finds nothing today, exactly as this story predicted rather than overselling.

The five split into two different kinds, which is why they get two different fixes.

**Not a defect, but the code was asking for it** — `tests/support/FixtureContent.cpp:31`
and `tests/unit/recovery/ManifestTest.cpp:66`, both building a `std::string` from a pair
of `istreambuf_iterator`s. At `-O2` GCC inlines libstdc++'s `sbumpc` and reports a
potential null dereference of `gptr()` inside `streambuf` — a path an `ifstream`'s buffer
cannot take, and one our code has no way to answer. The fix is neither a `-Wno-` nor a
pragma: the site now pumps the stream buffer through one out-of-line `operator<<`, which
asks the question once and outside our translation unit.

And the two sites were the *same function under two names*: `ManifestTest`'s local
`fileText` was a copy of `revenant::testing::readFileText`, which already existed in
`tests/support/`. The duplication gate never saw it — four lines is far under sixty
tokens — so the local copy is deleted and the test uses the shared helper. A third copy
of the idiom survives at `tests/integration/ImagegenRoundtripTest.cpp:33`; it returns
`std::vector<char>` rather than `std::string`, it produced no diagnostic, and unifying it
would mean rewriting that test's assertions, so it is recorded here rather than folded in.

**A real unchecked precondition** — `tests/unit/fs/ext4/DirectoryEntryTest.cpp:36`.
`writeLe` copied into `bytes.begin() + offset` with nothing guaranteeing the vector was
long enough; for a small enough `recordBytes` the destination really is empty and the
write really is out of bounds. GCC could not prove otherwise because it is not true. The
helper now checks the bound and fails its test rather than writing past the vector. No
caller in the file was passing a short spec, so nothing changes at runtime — what changes
is that a future one cannot do it silently.

None of the five was silenced. No `-Wno-` flag was added, no warning was dropped from
`CompilerWarnings.cmake`, and `REVENANT_WARNINGS_AS_ERRORS` is untouched.

## Acceptance criteria

- [ ] `ci.yml` contains no `-DREVENANT_BUILD_TESTS=OFF`; the release job configures
      with an unmodified `cmake --preset release`.
- [ ] The GCC release leg's log names test translation units
      (`revenant_tests.dir/...`), and the job fails if
      `build/release/tests/revenant_tests` is missing after the build.
- [ ] A clang leg builds the same preset at RelWithDebInfo with tests ON and
      `-Werror`, runs no `ctest`, and uploads no artifact.
- [ ] The `release-binaries` artifact contains the same four files as before this
      story: the two frontends, `revenant-imagegen`, and `build-info.json`.
- [ ] `benchmarks` still consumes that artifact and still passes; nothing about M7's
      packaging path changes.
- [ ] Every diagnostic the new legs report on the current tree is fixed, or recorded in
      this story per site with its reason. No warning is disabled to get green.
- [ ] Whole-run wall clock stays under 15 minutes, recorded in this story against the
      10m11s baseline measured on run `30540830546`.
- [ ] `ci.yml:178-181`'s comment no longer claims tests are off because the debug leg
      ran them; [quality-gates.md](../../testing/quality-gates.md) gate 5 — the owner
      of the warning contract — names the configurations it is enforced in.

## Test plan

**The run is the test — this story adds no code, so there is no unit seam.** What makes
it a test rather than a hope is that both directions are observed.

- *It compiles what it claims* (measured): the build log naming
  `tests/CMakeFiles/revenant_tests.dir/...` objects, plus the existence assertion on
  `build/release/tests/revenant_tests`, on both legs. Recorded here with the run URL.
- *It can fail for the reason it exists* — **demonstrated by the first real run, and the
  planted probe this story specified does not work.** Both are recorded, because the
  second is the more useful finding.

  The probe as written: revert `c2e8da0`'s guard in `include/revenant/core/Result.hpp`
  to the `hasValue()`-then-`get_if` shape and watch the GCC leg go red. Run 2026-08-01 on
  the WSL bench, GCC 14.2.0: **257 objects recompiled and the build stayed green.** The
  `-Wnull-dereference` that motivated `c2e8da0` is version-specific — it was reported by
  the GCC on CI's ubuntu image, and this one does not report it. A probe that fires only
  on some compilers cannot be the evidence that a leg works, and this story does not
  claim it as such. (What that says about the guard's comment at
  `Result.hpp:43-53` — that the shape is still the one satisfying both gates — is a
  question for whoever next edits it, not for this story.)

  What actually demonstrated it: the leg went red the first time it ran, on the real
  tree, with five `-Wnull-dereference` instances across two test translation units.
  Nothing was planted. That is a stronger form of the same evidence than a synthetic
  defect would have been, and it is recorded under *What the first run found* below.
- *Reviewed, not measured*: a clang-only optimized diagnostic. This repository has no
  instance of one to reach for, and manufacturing a construct until clang complains
  would be choosing the probe to fit the answer — the same error as tuning a threshold
  until the tree goes green. The clang leg's criterion is that it compiles the tests
  optimized and is green; its first real finding gets recorded when it happens.
- *Not automated*: the 15-minute budget. It is read off the run and written down, as
  the M5 note's numbers were.

## Definition of Done

- [ ] Acceptance criteria met, tests green under ASan + UBSan.
- [ ] clang-format, clang-tidy, duplication and file-length guard clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md))
      completed.
