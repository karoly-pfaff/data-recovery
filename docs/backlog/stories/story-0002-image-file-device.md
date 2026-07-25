<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0002: `ImageFileDevice` (portable image reader)

- Epic: [epic-m0-foundation](../epic-m0-foundation.md)
- Status: Done
- Size: M

## Goal

Provide the portable, privilege-free `BlockDevice` used throughout development and
testing: a read-only reader over a raw image file (`.dd`, `.img`). This is the default
source until physical-device access arrives in M4.

## Design references

- [I/O layer](../../architecture/io-layer.md)

## Acceptance criteria

- [x] `ImageFileDevice` opens an image path **read-only** and implements `BlockDevice`.
- [x] `sizeInBytes()` reflects the file size; `sectorSize()` defaults to 512 and is
      overridable at construction.
- [x] `readAt` uses positioned reads (`pread` on Linux, overlapped/offset read on
      Windows) with no shared mutable file offset (thread-safe reads).
- [x] Opening a missing/unreadable path returns a typed error, not a throw across the
      API boundary.
- [x] A read fault returns a typed `IoError` with offset and OS error code.

## Test plan

- [x] Integration: build a small temp image, read ranges, verify bytes match.
- [x] Unit: missing file → typed error; tail short read; concurrent `readAt` from
      multiple threads returns correct, non-interleaved data.

## Definition of Done

- [x] Acceptance criteria met; tests green under ASan on Windows (7/7 new
      `ImageFileDevice` tests, 37/37 total, `ctest --preset debug`). Linux/UBSan
      coverage is CI-only for this story — the POSIX implementation cannot be
      compiled on this Windows dev machine (see Known issues); same scope
      limitation as story-0001/story-0003/story-0005.
- [x] Platform code confined to `core/io/` (`ImageFileDevicePosix.cpp` /
      `ImageFileDeviceWindows.cpp`), selected by `src/CMakeLists.txt` via
      `if(WIN32)` — no `#ifdef` in shared code.
- [x] Coverage held or raised (no local coverage-preset run this story — same
      scope limitation as story-0001). Lint/format/duplication/file-length
      guards clean: `guard-limits` and `format-check` pass and cover **both**
      platform files (format-check is compilation-independent). `tidy` (release
      preset) exits 0 on Windows, but that only ever exercises the Windows TU —
      `ImageFileDevicePosix.cpp` is filtered out of the local `tidy` target
      because it isn't a compilable TU here (see Known issues item 13). It has
      instead been **hand-audited** against the exact checks the Windows file
      needed (function-size, misc-include-cleaner, designated-initializers,
      named-parameter, swappable-parameters) and restructured to mirror the
      Windows file's proven, split shape; it remains **CI-verified, not
      locally-tidy-verified**, on Linux. `jscpd --min-lines 8 --threshold 0 src
      include` reports 0 clones (no `ReadRange` extraction needed).
- [x] `CHANGELOG.md` updated; story-level self-audit completed.

## Known issues

Small, noted, behavior-preserving deviations from the brief's verbatim text where the
local toolchain (MSVC + clang-tidy v22) demanded it, following the precedent set by
story-0001/story-0003/story-0005. None change the `ImageFileDevice`/`TempFile` API
surface — every signature the brief marked as consumed by later tasks matches exactly.

1. **Missing `<array>` in `ImageFileDeviceTest.cpp`.** The brief's verbatim test file
   uses `std::array<bool, 4>` without including `<array>`. MSVC's STL does not
   transitively provide it via `<thread>`/`<vector>`, so the file failed to compile
   (`C2079`/`C2109`/…) until `<array>` was added. This is a build-correctness fix, not
   a style accommodation.
2. **`misc-include-cleaner` (header hygiene, v22).** Added direct includes: `Result.hpp`
   to `ImageFileDevice.hpp`; `Error.hpp`, `Result.hpp`, `<cstddef>`, `<cstdint>`,
   `<filesystem>`, `<memory>`, `<span>`, `<utility>` to `ImageFileDeviceWindows.cpp`;
   `Error.hpp`, `BlockDevice.hpp`, `<functional>` to `ImageFileDeviceTest.cpp` (and
   removed `<cstdint>`, flagged as unused-direct); `<filesystem>`, `<ios>`,
   `<system_error>`, `<vector>`, `<cstddef>` to `TempFile.cpp`.
3. **`cppcoreguidelines-special-member-functions`.** `ImageFileDevice.hpp` now
   explicitly `= delete`s copy/move construction and assignment alongside its
   destructor (rule of five). `BlockDevice` already deletes these, so this is
   restating an existing invariant, not a behavior change.
4. **`modernize-use-designated-initializers`.** Every `Error{...}` positional
   aggregate-init in `ImageFileDeviceWindows.cpp` now uses `.code=`/`.offset=`/
   `.osCode=` (field order unchanged). Semantically identical; same accommodation as
   story-0001.
5. **`cppcoreguidelines-pro-bounds-avoid-unchecked-container-access`.** `operator[]`
   accesses in `ImageFileDeviceTest.cpp` (`bytes[i]`, `buffer[0]`, `results[i]`) now
   use `.at(...)`. Same accommodation as story-0001; all accesses were already
   range-guarded by construction.
6. **`clang-format` line-wrapping and include-ordering.** The brief's hand-wrapped
   continuation lines didn't match the project's 100-column/`BinPackParameters: false`
   style; reformatted in place with `clang-format -i`. The project's
   `IncludeCategories` also sorts the quoted "main" header include *after* the angle-
   bracket blocks when the source file's basename doesn't match the header's (e.g.
   `ImageFileDeviceWindows.cpp` vs. `ImageFileDevice.hpp` — no `IncludeIsMainRegex`
   match), unlike same-basename pairs such as `InMemoryDevice.cpp`/`.hpp` in story-0001.
   Purely whitespace/ordering — no semantic change.
7. **`readability-function-size` (10-statement limit) — mechanical helper-splits,
   applied identically on both platform files.** `ImageFileDeviceWindows.cpp`'s
   `open()` (hand-counted 14 statements) split into `openHandle` (`CreateFileW` +
   validate), `queryFileSize` (`GetFileSizeEx` + validate, closing the handle on
   failure), and `openImage` (composes the two) — each a genuine single-purpose
   step, not just a line-count dodge. `readFully` (12 statements) restructured to
   merge its two loop-exit guards (I/O error vs. EOF) into one `if`, using
   `advanceByOneChunk`'s "same total = EOF" return convention documented inline.
   `ImageFileDevicePosix.cpp`'s structurally-identical `readFully` (hand-counted 15
   statements) and `open` (14 statements) were **initially left unsplit** (per the
   original instruction not to modify that file speculatively), then split to
   mirror the Windows shape exactly once code review found the plan's 10-statement
   hard limit overrides the brief's verbatim block: `readFully` now delegates a
   single `advanceByOneChunk` (the `pread` attempt, EINTR-retried internally via a
   `do`/`while`, folded into the running total — hand-counted 8 statements) driven
   by the same 9-statement loop shape as Windows; `open` now delegates
   `openFd`/`queryFileSize`/`openImage` (hand-counted 5/7/9 statements) and is
   itself 8 statements. Behavior is unchanged (verified against every existing
   test that exercises the Windows twin of this logic; the POSIX file itself
   cannot be compiled/run locally — see Known issues item 13 and the fix report
   for the full hand-count table).
8. **Windows-API `misc-include-cleaner` false positives — judgment call.**
   `<windows.h>` and the ~15 Win32 symbols it provides (`HANDLE`, `OVERLAPPED`,
   `DWORD`, `ReadFile`, `CreateFileW`, `GetFileSizeEx`, etc.) are flagged individually
   ("no header providing X is directly included", and even "windows.h is not used
   directly"). clang-tidy's IWYU mapping has no entries for windows.h's internal,
   not-standalone-includable constituent headers (`fileapi.h`, `handleapi.h`,
   `winbase.h`, …), so per-symbol direct includes aren't achievable. Suppressed with a
   file-scoped `NOLINTBEGIN(misc-include-cleaner)`/`NOLINTEND(misc-include-cleaner)`
   bracketing the Win32-touching code, with an inline justification comment. Not one
   of the three pre-authorized accommodation categories, so flagged here explicitly —
   same judgment-call pattern as story-0001's item 4.
9. **`cppcoreguidelines-pro-type-union-access` on `OVERLAPPED.Offset`/`.OffsetHigh`.**
   These fields live in an anonymous union mandated by the Win32 struct layout; there
   is no alternative access pattern (`boost::variant` is not applicable to an OS ABI
   struct). Suppressed with per-statement `NOLINTNEXTLINE`, judgment call as above.
10. **`bugprone-easily-swappable-parameters` on the `ImageFileDevice` constructor.**
    `(nativeHandle, sizeBytes, sectorSize)` are three adjacent, mutually-convertible
    integral types — but the signature is the brief's verbatim, interface-consumed-by-
    later-tasks header declaration, and the constructor is reachable only from
    `open()`'s single call site via the `ConstructTag` guard. Suppressed with
    `NOLINTBEGIN`/`NOLINTEND`, judgment call as above (same pattern as story-0001's
    item 4, applied to a constructor instead of a test helper).
11. **`readability-named-parameter` on the unnamed `ConstructTag` parameter.**
    Changed to `ConstructTag /*unused*/` in the `.cpp` definition only (the `.hpp`
    declaration, which is not itself flagged, is untouched) — this is the exact
    fix-it text clang-tidy's own diagnostic suggests; a comment, not a real
    identifier, so it does not introduce an unused-parameter warning.
12. **`misc-misplaced-const` on `const HANDLE handle = ::CreateFileW(...)`.** `HANDLE`
    is `typedef void*`; `const HANDLE` means "constant pointer to void", not "pointer
    to const void", which the check flags as likely-unintended. Dropped the `const`
    (the local is never reassigned in practice either way) rather than suppress — a
    genuine, zero-behavior-change fix.
13. **`tidy` target processed `ImageFileDevicePosix.cpp` despite the brief's
    assumption otherwise — environment finding, fixed in this story's scope.**
    The brief states clang-tidy "runs on the Windows source only (the POSIX file
    isn't in the compile database on this platform)". Observed behavior initially
    contradicted this: `cmake/DevTargets.cmake`'s `tidy` target built its file list
    via `file(GLOB_RECURSE)` over `src/`/`include/`/`tests/`, independent of
    `compile_commands.json`, so it **did** pass `ImageFileDevicePosix.cpp` to
    `clang-tidy -p <builddir>`, which fell back to default flags lacking POSIX
    header search paths and failed with a hard preprocessor error
    (`unistd.h file not found`). Story-0002's own Definition of Done requires
    "platform code confined to `core/io/`, selected by CMake" — the quality
    tooling has to cope with a platform-split source tree, and the Linux CI `tidy`
    job would symmetrically hard-fail on `ImageFileDeviceWindows.cpp`/`windows.h`.
    **Fixed** in `cmake/DevTargets.cmake`: `format`/`format-check` (compilation-
    independent) and `guard-limits` (directory-based) still use the unfiltered
    `revenant_sources` and cover **both** platform files; a new
    `revenant_tidy_sources` list, derived only for the `tidy` target, excludes the
    off-platform implementation by filename regex (`Posix\.cpp$` on Windows,
    `Windows\.cpp$` elsewhere). `tidy` (release preset) now exits 0 on Windows,
    tidying `ImageFileDeviceWindows.cpp` and skipping the POSIX file.
    **Honest, permanent asymmetry (by design, not a gap):** `ImageFileDevicePosix.cpp`
    is *tool*-tidied only by Linux CI, never on this Windows dev machine;
    `ImageFileDeviceWindows.cpp` is *tool*-tidied only on Windows, never on Linux.
    Each platform's `tidy` run can only ever cover its own implementation file,
    since the other one isn't a compilable TU there — this is inherent to having
    platform-selected sources at all, not something further tooling changes can
    close. What *is* now closed: item 14 below applies the same tidy-driven
    structural/hygiene fixes to the POSIX file by hand, so it is no longer merely
    "hoping CI is fine with it" — it mirrors code already proven clean under the
    same check set on Windows.
14. **`ImageFileDevicePosix.cpp` hygiene/structure mirrored from the Windows
    file, hand-audited (not tool-verified) — Important findings from code review,
    fixed.** Applied every accommodation the Windows file needed under the
    identical clang-tidy checks, since the plan's 10-statement hard limit
    (`readability-function-size`, `WarningsAsErrors: '*'`) is a merge gate that
    overrides the brief's verbatim POSIX code block: direct includes for
    `revenant/core/Error.hpp`, `revenant/core/Result.hpp`, `<cstddef>`,
    `<cstdint>`, `<filesystem>`, `<memory>`, `<span>`, `<utility>`
    (`misc-include-cleaner`); `ConstructTag /*unused*/` in the `.cpp` definition
    (`readability-named-parameter`); every `Error{...}` converted to designated
    initializers (`modernize-use-designated-initializers`); the constructor
    bracketed with `NOLINTBEGIN`/`NOLINTEND(bugprone-easily-swappable-parameters)`,
    same justification as Windows. The function-size split is item 7's `openFd`/
    `queryFileSize`/`openImage`/`advanceByOneChunk`/`readFully` — see that item
    and the fix report for the full hand-counted statement table (every touched
    function is 5–9 statements, with margin under the 10 threshold). Because this
    file cannot be compiled or clang-tidy'd on this Windows machine, these fixes
    are **hand-audited against the same rule set proven correct on the Windows
    twin, not independently tool-verified** — Linux CI's `tidy` job is the actual
    verifier and may still surface something this hand-audit missed.
15. **`threads.reserve(results.size())` in `ImageFileDeviceTest.cpp` — previously
    undocumented (reviewer Minor).** `performance-inefficient-vector-operation`
    flagged `threads.emplace_back(...)` inside the loop in
    `ConcurrentReadsDoNotInterleave`; added a `reserve` call before the loop.
    Zero behavior change (same 4 threads constructed either way), purely avoids a
    reallocation the linter can prove is avoidable. Was applied during the
    original implementation pass but not previously listed here; now recorded.

## Concerns for follow-up stories

- `ImageFileDevicePosix.cpp` is transcribed from the brief's logic verbatim (byte
  reading semantics unchanged) but restructured to mirror the Windows file's
  proven shape, and has **not** been compiled or clang-tidy'd locally (no POSIX
  toolchain on this Windows dev machine) — it is CI-verified only. Every touched
  function was hand-counted against the same statement-counting rule that was
  empirically validated against real clang-tidy output on the Windows file (see
  item 14 and the fix report), but hand-counting is not a substitute for the
  actual tool; Linux CI's first `tidy` run against this file is the true
  verification and should be watched closely the first time this branch's CI runs.

## Story-level self-audit (docs/code-quality.md)

- Responsibility & clarity: yes — `ImageFileDevice` does one thing (BlockDevice over an
  image file); after the function-size split, applied identically on both platform
  files, `open()`/`openHandle()`(or `openFd()`)/`queryFileSize()`/`openImage()` and
  `readFully()`/`advanceByOneChunk()`(Windows also has `readChunk()` underneath, since
  its single OS call doesn't retry; POSIX folds the EINTR retry directly into
  `advanceByOneChunk()`) each do exactly one step of the platform read path, nameable
  from their signatures alone; each file has one responsibility (interface,
  per-platform I/O, temp-file test support, integration tests).
- Design: `ImageFileDevice` has one reason to change (SRP); tests consume it through
  `BlockDevice&` where relevant (`readSlice`), matching story-0001's DIP precedent; no
  speculative abstraction — `ReadRange.hpp` was *not* added since jscpd found 0 clones,
  exactly as the brief predicted; no duplicated knowledge between the platform files
  (their shared shape is convergent, not copy-paste — see brief's own note).
- Anti-patterns: none introduced — no God object, no boolean-parameter traps, no
  nesting beyond what `InsertBraces: true` mandates, no dead or commented-out code.
- Correctness & safety: every error path is a typed `Result`/`Error` value, nothing
  swallowed; the source-image read-only guarantee holds structurally (`GENERIC_READ`/
  `O_RDONLY`, no write call anywhere in either platform file); byte handling is UB-free
  (`std::span`, `std::bit_cast` for the `HANDLE`↔`intptr_t` round trip, explicit
  overflow check before any offset arithmetic, no `reinterpret_cast`).
- Tests: written test-first (RED confirmed — build failed on the missing
  `ImageFileDevice.hpp` before implementation existed); cover exact-byte reads, size/
  sector reporting, sector-size override, zero-sector-size rejection, tail short read,
  missing-file typed error, and 4-thread concurrent non-interleaved reads (green under
  ASan). `ImageFileDevice` is not itself a byte-parser (no untrusted format bytes
  parsed here), so no fuzz target applies — matches the interface-only scope of
  story-0001. The POSIX file's post-split structure was verified two ways short of
  compiling it: (1) hand-counting every touched function's statements against the
  exact rule that reproduced clang-tidy's real reported counts on the Windows file
  (see the fix report's count table — every function lands at 5–9, not 10), and
  (2) a line-by-line behavior diff against the Windows twin it mirrors, confirming
  the EINTR-retry/error/EOF/accumulate semantics are unchanged. Linux CI's `tidy`
  and test jobs remain the actual verification for this file.

## Addendum (2026-07-25)

- The Design bullet above ("`ReadRange.hpp` was *not* added since jscpd found 0
  clones") is **superseded**. The dedup hotfix commit `5ed8d05` ("refactor(core):
  deduplicate ImageFileDevice platform files via shared read-range core") found
  the two platform files *had* drifted into a jscpd-detectable clone after all
  (see story-0007's Definition of Done, the "4 pre-existing clones" line) and
  introduced `include/revenant/core/io/ReadRange.hpp` plus the unconditionally-
  compiled `src/core/io/ImageFileDeviceShared.cpp` to extract the shared
  `readAt`/`open`/destructor logic and the `clampReadRange`/`driveReadLoop`/
  `openWithSize` helpers, zeroing the clone without suppression.
- This fix commit (first-Linux-CI-contact hardening, `fix(build): make the
  first Linux CI contact green`) reshaped `ImageFileDevicePosix.cpp`'s
  `advanceByOneChunk` on top of that `ReadRange.hpp` split: `buffer.data() +
  total` became `buffer.subspan(total)` (mirroring the Windows twin's
  `readChunk` subspan pattern, fixing `cppcoreguidelines-pro-bounds-pointer-
  arithmetic`), and the EINTR-retry `do { … } while (...)` became a
  `for (;;) { …; if (...) break; }` loop with identical retry semantics
  (fixing `cppcoreguidelines-avoid-do-while`). Both findings came from a
  tidy-driven review; the change is hand-audited only, not locally
  tidy-verified — see Known issues item 14 and the Concerns section above for
  why the POSIX TU can't be compiled on the Windows dev machine.
