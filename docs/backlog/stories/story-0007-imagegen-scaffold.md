<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0007: Synthetic-image generator scaffold in `tools/`

- Epic: [epic-m0-foundation](../epic-m0-foundation.md)
- Status: Done
- Size: M

## Goal

A deterministic synthetic-image generator scaffold: the tool the test corpus grows
from (NTFS builders arrive in M1). Generates byte-pattern images that make offset
math verifiable.

## Acceptance criteria

- [x] Three patterns as specified: `zero` (all-zero sectors), `counter` (byte `j` of
      sector `n` equals `(n*512 + j) & 0xFF`), `lba` (zeros with the LE64 sector
      number stamped in the first 8 bytes).
- [x] Identical inputs produce byte-identical images (no timestamps, no randomness,
      no filesystem-order dependence).
- [x] Generated images round-trip through `ImageFileDevice` (read sector tags back
      exactly as written).
- [x] Usage errors (wrong argument count, unparseable size, unknown pattern name)
      exit non-zero with a clear message on stderr.
- [x] The tool's C++ (`tools/imagegen/`) falls under every quality gate: the lint
      globs (`cmake/DevTargets.cmake`, `.github/workflows/ci.yml`) and the
      duplication scan now cover `tools/`.

## Test plan

- Unit (`tests/unit/tools/PatternWriterTest.cpp`): `fillSector` per pattern (zero,
  counter, LBA tag, including a pre-poisoned buffer to prove overwrite); `parsePattern`
  valid names (`zero`/`counter`/`lba`) and an unknown name (typed
  `kInvalidArgument`).
- Unit (`tests/unit/tools/ImagegenCliTest.cpp`): valid CLI arguments generate the
  exact-size file and return `true`; an unparseable size and an unknown pattern name
  both return `false`.
- Integration (`tests/integration/ImagegenRoundtripTest.cpp`): generate an LBA-tagged
  image, open it with `ImageFileDevice`, read sector 7 back, and verify the decoded
  LE64 tag equals `7`; write the same counter-pattern image twice and diff the raw
  bytes for determinism; write a size that isn't a whole number of sectors and verify
  the partial tail sector is still written (exact file size).
- Determinism: covered by `ImagegenRoundtrip.GenerationIsDeterministic` (two
  independent `writeImage` calls, byte-for-byte file comparison).
- Fuzz: none — no external-byte parsing in this story. The CLI parses `argv`
  (a small, fully-enumerated grammar), covered by the unit tests above; nothing here
  parses untrusted disk/image bytes yet (that starts with the M1 filesystem/carve
  parsers).

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan+UBSan; `ctest --preset debug` →
      53/53, the 40 pre-existing plus 13 new: 2 `Endian.Writes*`/`Endian.*Roundtrips`,
      5 `PatternWriter.*`, 3 `ImagegenCli.*`, 3 `ImagegenRoundtrip.*`).
- [ ] Coverage held or raised (>= 85% core). Not measured locally this story (same
      scope limitation noted in stories 0003–0006: the `coverage` preset was not run
      in this session). `tools/` is intentionally outside the core-coverage
      `--prefix src --prefix include` gate (the gate has always scoped to
      `src`/`include`; `tools/` sources are the developer-tool layer, not core
      recovery logic), so this story cannot move that number either way.
- [x] clang-format, clang-tidy, duplication, file-length guard clean. `guard-limits`
      and `format-check` (debug preset) and `tidy` (release preset, per the
      story-0005 Windows-ASan-vs-clang-tidy precedent) all exit 0 — see "Known
      issues" below for the fixes that got them there. `jscpd@4.0.5 --min-lines 8
      --threshold 0 src include tools` still reports the same 4 pre-existing
      clones (6.6% -> 4.47%, same absolute lines) confined entirely to
      `src/core/io/ImageFileDevicePosix.cpp` vs `ImageFileDeviceWindows.cpp`
      (story-0002 files, zero diff from `main` on this branch) — `tools/imagegen`
      contributes zero new duplication; the pre-existing baseline failure is
      out of this story's scope (see task-7-report.md for the parity check).
      **Resolved by `5ed8d05`**: the dedup hotfix extracted the shared logic
      into `ReadRange.hpp`/`ImageFileDeviceShared.cpp`, and these 4 clones are
      gone (0 clones as of that commit).
- [x] CHANGELOG.md updated under [Unreleased].
- [x] Story-level self-audit checklist (docs/code-quality.md) completed (below).
- [x] Docs/ADRs updated if the design changed (no design change; N/A).

## Known issues

Resolved per the same "small, noted deviation" precedent as stories 0003–0006 — none
change the consumed API surface (`toLittleEndian`, `Pattern`, `parsePattern`,
`fillSector`, `writeImage`, `runCli` all match the brief's signatures exactly).

1. **`std::filesystem::remove` sharing violation on Windows
   (`ImagegenRoundtripTest.cpp`).** The brief's verbatim
   `LbaTagsReadBackThroughImageFileDevice` test calls
   `std::filesystem::remove(path)` (the throwing overload) while `device` — an
   `ImageFileDevice` holding an open `HANDLE` — is still in scope.
   `ImageFileDeviceWindows.cpp` (story-0002, out of this story's scope) opens with
   `FILE_SHARE_READ | FILE_SHARE_WRITE` but not `FILE_SHARE_DELETE`, so Windows
   refuses the delete while the handle is open and the call throws
   `std::filesystem::filesystem_error` ("The process cannot access the file because
   it is being used by another process"). The existing `tests/support/TempFile.cpp`
   helper already routes around this exact OS behavior (it uses the non-throwing
   `remove(path, ec)` overload and ignores the error). Fix here: release the device
   before deleting — `device.value().reset();` immediately before the `remove` call
   — a one-line, semantics-identical addition (RAII close-then-delete) that changes
   no assertion and no production interface. Verified: the test failed with the
   exact exception message above before the fix, and passes after it.
2. **`clang-format` on the brief's verbatim bodies.** Several of the brief's inline
   snippets (multi-line function signatures, an inline lambda in
   `CliMain.cpp::parseArgs`, aligned enumerator comments in `PatternWriter.hpp`) are
   not clang-format-clean under this repo's `.clang-format`. Fixed by running
   `cmake --build --preset debug --target format` (repeated after each round of
   hand-edits below); every change is whitespace-only (confirmed by re-running the
   full `ctest` suite after each reformat: still 53/53).
3. **`misc-include-cleaner` (direct includes), story-0002/0004/0005 precedent.**
   Added includes for symbols the brief's files use but only pick up transitively:
   `<span>`/`revenant/core/Error.hpp`/`revenant/core/Result.hpp`/`<system_error>`/
   `revenant/core/log/LogLevel.hpp` to `CliMain.cpp`; `<span>`/`<cstddef>`/
   `<cstdint>`/`<filesystem>`/`<ios>`/`<string_view>`/`revenant/core/Error.hpp`/
   `revenant/core/Result.hpp` to `PatternWriter.cpp`; `<cstddef>` to
   `EndianWriteTest.cpp`; `revenant/core/Error.hpp` to `PatternWriterTest.cpp`;
   `<cstdint>`/`<ios>`/`<span>` to `ImagegenRoundtripTest.cpp`.
4. **`modernize-use-designated-initializers`.** `Error{ErrorCode::kInvalidArgument}`
   (three call sites) now reads `Error{.code = ErrorCode::kInvalidArgument}`;
   `Error{ErrorCode::kIoFailure, written}` now reads
   `Error{.code = ErrorCode::kIoFailure, .offset = written}`;
   `GenerateRequest{args[1], size.value(), pattern}` now reads
   `GenerateRequest{.outputPath = args[1], .sizeBytes = size.value(), .pattern =
   pattern}`. Same pattern as story-0003's `Error{...}` fix; field order unchanged.
5. **`cppcoreguidelines-pro-bounds-avoid-unchecked-container-access` on
   `std::array` (test files).** `std::array` has `.at()`, so — same precedent as
   stories 0001/0002/0004 — `bytes[0]`/`bytes[7]` (`EndianWriteTest.cpp`),
   `sector[...]` (`PatternWriterTest.cpp`, 7 sites), and `args[0..3].data()`
   (`ImagegenCliTest.cpp`, the brief's own argv-mutation pattern the task brief
   pre-authorized a "small semantics-identical accommodation" for) all now use
   `.at(...)`. Every index was already in-range by construction; `.at()` only adds
   a redundant, never-taken throw path.
6. **`cppcoreguidelines-pro-bounds-avoid-unchecked-container-access` on
   `std::span` (production code).** `std::span` has no checked accessor in C++20
   (`.at()` isn't added until C++26) and is a genuinely different type from
   `std::array` above — no drop-in fix exists. `sector[j]` in
   `PatternWriter.cpp::fillCounter` and `args[1..3]` in `CliMain.cpp::parseArgs`
   are both already guarded (the loop condition `j < sector.size()`; the
   `args.size() != kExpectedArgs` early return, respectively) — scoped
   `NOLINT(NEXT)?LINE` with an inline justification comment, per AGENTS.md's "no
   suppressions without an inline justification."
7. **`cppcoreguidelines-pro-bounds-pointer-arithmetic` on `std::from_chars`.**
   `CliMain.cpp::parseSize` originally tried `text.begin()`/`text.end()` (no
   explicit `+`) instead of the brief's `text.data()`/`text.data() + text.size()`,
   to avoid the pointer-arithmetic warning outright — but MSVC's Debug-mode
   `string_view` iterator is a bounds-checked wrapper type, not a raw
   `const char*`, and doesn't interoperate with the `const char*`-returning
   `from_chars` overload's result comparison (`end != text.end()` failed to
   compile: no matching `operator!=`). Reverted to the brief's `.data()`/
   `.data() + .size()` form with a scoped `NOLINTBEGIN/END` — `from_chars`'s
   `[first, last)` pointer-pair signature is the only one portable across both
   toolchains here; the arithmetic never leaves one already-bounded
   `string_view`.
8. **`readability-math-missing-parentheses`.** `(lba * kSectorBytes + j)` in
   `PatternWriter.cpp::fillCounter` and `(2U * kSectorBytes + 5U)` in
   `PatternWriterTest.cpp` both now explicitly parenthesize the multiplication:
   `((lba * kSectorBytes) + j)` / `((2U * kSectorBytes) + 5U)`. Precedence and
   value are unchanged; the check wants the grouping spelled out.
9. **`readability-function-size` (>10 statements).** `CliMain.cpp::runCli` (13
   statements) and `PatternWriter.cpp::writeImage` (13 statements) each exceeded
   the 10-statement hard limit once written out per the brief. Mechanical helper
   splits, each a single extracted responsibility: `runCli` -> `reportUsageError`
   (log the usage message, return false) + `generateAndReport` (call `writeImage`,
   log/return on failure) + a now 4-statement `runCli` that just dispatches;
   `writeImage` -> `writeChunk` (fill and write one sector-or-tail chunk, return
   the new running total) + a now 5-statement `writeImage` loop. No behavior
   change — confirmed by the unchanged 53/53 `ctest` result after each split.
10. **`clang-analyzer-optin.core.EnumCastOutOfRange` inside MSVC's own
    `<filesystem>` header, not project code.** Every test that calls
    `std::filesystem::file_size` (`ImagegenRoundtripTest.cpp`'s
    `PartialTailSectorIsWritten`, `ImagegenCliTest.cpp`'s
    `GeneratesImageFromValidArguments`, both brief-verbatim) transitively
    triggers a clang-analyzer false positive *inside* MSVC's
    `xfilesystem_abi.h`: `__std_fs_stats_flags::_Follow_symlinks |
    __std_fs_stats_flags::_File_size` (a legitimate bitmask OR, value 9) gets
    flagged as "not in the valid range" for that enum, because clang's analyzer
    doesn't know the enum is a bitmask. The diagnostic's file:line is inside a
    Visual Studio system header we don't own and can't edit, so no `NOLINT`
    comment can target it directly. Fixed by excluding
    `clang-analyzer-optin.core.EnumCastOutOfRange` in `tests/.clang-tidy` only
    (`InheritParentConfig: true`, so this narrows just the tests/ tree; the root
    `.clang-tidy` — and therefore all of `src/`/`include`/`tools/` — keeps the
    check enabled). Scoped this way rather than in the root config because no
    production code calls `std::filesystem::file_size`; if it ever does, the
    check should still fire there. Same "narrow, documented relaxation" framing
    the file already uses for its three pre-existing exclusions, and the same
    precedent as story-0005's `-portability-avoid-pragma-once` root-config
    exclusion (a maintainer ruling recorded inline, not a silent suppression).

Verification transcript (`cmake --build --preset release --target tidy`, after all
fixes above): every one of the 39 tidied files (`revenant_tidy_sources`, which now
globs `tools/*.cpp`/`tools/*.hpp` too) processed with zero warnings promoted to
errors — `Suppressed 226081 warnings (226046 in non-user code, 35 NOLINT)`, exit
code 0. Full transcript in task-7-report.md.

## Story-level self-audit (docs/code-quality.md)

- Responsibility & clarity: yes — `parsePattern` maps a name to an enumerator and
  nothing else; `fillSector` dispatches to one of three single-purpose helpers
  (`fillCounter`, `fillLbaTag`, or an inline zero-fill) by pattern; `writeImage`
  does exactly one thing (stream sectors to a file, sector by sector, tracking bytes
  written); `runCli` does parse-then-generate-then-report, each step one call
  (`parseArgs`, `writeImage`, `logger.log`). Every file has one responsibility:
  `PatternWriter.{hpp,cpp}` is pattern generation, `CliMain.{hpp,cpp}` is argument
  parsing and orchestration, `Main.cpp` is the process entry point only.
- Design: `Pattern`/`parsePattern`/`fillSector`/`writeImage` have one reason to
  change each (the pattern set, the parser grammar, one sector's bytes, and the
  file-writing loop, respectively — four separate concerns kept in four separate
  functions). `runCli` depends on `Logger`/`LogSink` by injected reference/interface
  (DIP), not a concrete global logger. No speculative generality: no extra patterns,
  no configurable sector size, no output-format options beyond what the three ACs
  and the tests need (YAGNI). No duplicated knowledge — `asChars` is the single
  place that turns a `std::byte` sector into `ofstream`-writable bytes; the sector
  loop in `writeImage` is the single place that knows about partial tail sectors.
- Anti-patterns: none introduced — no God object (the CLI, the pattern logic, and
  the entry point are three separate translation units); no boolean-parameter traps
  (`Pattern` is a named enum, not a bool); no nesting beyond 2 levels; no dead or
  commented-out code.
- Correctness & safety: every fallible path returns `Result<T>` — `parsePattern`,
  `writeImage`, `parseSize`, `parseArgs` never throw or swallow a failure; `runCli`
  checks `hasValue()` before touching `.value()` on both the parse and the write
  step. The source-device read-only guarantee is untouched by this story (imagegen
  only ever creates new synthetic files under a caller-given output path — it is
  never pointed at a real recovery source). Byte handling is UB-free: `fillSector`
  and `writeImage` operate through `std::span`, `asChars` uses `std::bit_cast` (no
  `reinterpret_cast`), and `toLittleEndian` mirrors `fromLittleEndian`'s existing
  `bit_cast`-based, sanitizer-clean pattern.
- Tests: written test-first for both cycles — RED confirmed for `toLittleEndian`
  (`C2039: 'toLittleEndian': is not a member of 'revenant'`) before the
  implementation existed, and RED confirmed for the generator (`C1083: Cannot open
  include file: 'imagegen/PatternWriter.hpp'` / `'imagegen/CliMain.hpp'`) before any
  `tools/imagegen/*` file existed. Tests cover edge/malformed inputs: a
  pre-poisoned (`0xAA`/`0xFF`-filled) buffer for both `kZero` and `kLbaTag` to prove
  the fill actually overwrites; an unknown pattern name; a wrong CLI argument count
  is not separately asserted but is exercised indirectly (empty/garbage args always
  fail `parseArgs`'s size check); a size string that fails `from_chars`; a
  non-sector-aligned total size (partial tail sector). No new byte-parser reads
  externally-sourced/untrusted bytes in this story (imagegen only *writes*
  self-generated bytes), so no fuzz target applies, per AGENTS.md §4 ("every parser
  that reads external bytes has a fuzz target") — `argv` parsing is covered by unit
  tests instead, as scoped in the test plan above.
