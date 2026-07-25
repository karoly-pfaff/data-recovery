<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0005: CMake library/test wiring for `librevenant`

- Epic: [epic-m0-foundation](../epic-m0-foundation.md)
- Status: Done
- Size: S

## Goal

Activate the build: a static `librevenant` with one real symbol, a GoogleTest
test target wired through vcpkg, and CI jobs that genuinely build, test, and
gate on both platforms (they currently skip on "no sources").

## Acceptance criteria

- [x] `cmake --preset debug && cmake --build --preset debug && ctest --preset debug`
      is green locally with ASan(+UBSan on Linux) enabled.
- [x] `librevenant` exposes `revenant::version()` returning the project version,
      covered by a unit test.
- [x] CI `build-test` runs for real on Windows and Linux (MSVC dev env + vcpkg
      bootstrap fixed); `guards` duplication gate no longer ignores failures.
- [x] MSVC ASan builds despite CMake's default `/RTC1` Debug flag.

## Test plan

- Unit: `revenant::version()` equals the configured project version.
- Meta: `ctest` discovers and runs the test on both platforms under sanitizers.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan+UBSan).
- [ ] Coverage held or raised (>= 85% core). Not measured this story: the local
      verification scope for task-1 excluded the coverage preset; no coverage
      baseline existed before this story to compare against.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
      clang-format and the file-length guard are clean. `clang-tidy` is clean
      when run from the `release` preset (see "Known issues" below for why
      `release`, not `debug`, on Windows). The duplication detector has no
      local CMake target, so it was run directly:
      `npx --yes jscpd@4.0.5 --min-lines 8 --threshold 0 src include` →
      `Found 0 clones` (exit 0).
- [x] CHANGELOG.md updated under [Unreleased].
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Docs/ADRs updated if the design changed (no design change; N/A).

## Known issues

Resolved per maintainer ruling:

1. **`-MDd` vs `-fsanitize=address`.** clang-tidy's Clang frontend refuses
   the `/MDd` + `/fsanitize=address` combination that MSVC's `cl.exe` itself
   accepts. Fix: run the `tidy` target from the `release` preset on Windows
   (no sanitizers there), documented in
   `docs/testing/quality-gates.md` under "Local pre-flight". Verified clean:
   `cmake --preset release && cmake --build --preset release --target tidy`.
2. **`portability-avoid-pragma-once` on `Version.hpp`.** `#pragma once` is
   the project convention (used throughout the M0 foundation plan); the
   check's exotic-filesystem portability concerns don't apply to our target
   platforms. Fix: excluded `-portability-avoid-pragma-once` in the root
   `.clang-tidy`, with a comment recording the ruling.
3. **`misc-include-cleaner` on `Version.cpp`.** Fix: added a direct
   `#include <string_view>` to `src/core/Version.cpp` (authorized deviation
   from the brief's verbatim content).

Verification transcript (`cmake --build --preset release --target tidy`,
after fixes 1–3 above):

```
[1/2] clang-tidy: static analysis
[1/3] Processing file .../include/revenant/core/Version.hpp.
4709 warnings generated.
[2/3] Processing file .../src/core/Version.cpp.
9418 warnings generated.
[3/3] Processing file .../tests/unit/core/VersionTest.cpp.
14848 warnings generated.
Suppressed 14848 warnings (14848 in non-user code).
```

Exit code 0 — no errors, all findings suppressed as non-user-code noise.
