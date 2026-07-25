<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0004: Logging facility (leveled, testable sink)

- Epic: [epic-m0-foundation](../epic-m0-foundation.md)
- Status: Done
- Size: S

## Goal

A minimal leveled logging facility with dependency-injected sinks, so device and
recovery code can report diagnostics without global state and tests can assert on log
output.

## Acceptance criteria

- [x] Messages below the `Logger`'s `minLevel` are filtered (not forwarded to the sink).
- [x] Messages at or above `minLevel` are forwarded to the sink verbatim, with their
      level.
- [x] The sink is injected (dependency injection) — there is no global logger.
- [x] A `StderrSink` exists for CLI tools.
- [x] `toString(LogLevel)` covers every enumerator.

## Test plan

- [x] Unit: a message below `minLevel` is filtered (not forwarded to the sink).
- [x] Unit: messages at and above `minLevel` are forwarded, with level and message
      intact.
- [x] Unit: `toString` returns the expected name for every `LogLevel` enumerator.
- No byte parsing in this story → no fuzz target.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan+UBSan; 24/24, `ctest --preset debug`).
- [ ] Coverage held or raised (>= 85% core). Not measured this story: same local
      verification-scope limitation noted in story-0003/story-0005 — the coverage
      preset was not run locally. `StderrSink::write` (1 line) is intentionally
      untested here per the brief; it is exercised by Task 7's CLI.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
      `cmake --build --preset debug --target format-check` and `--target
      guard-limits` clean; `cmake --build --preset release --target tidy` clean
      (see "Known issues"); `npx --yes jscpd@4.0.5 --min-lines 8 --threshold 0 src
      include` → `Found 0 clones` (exit 0).
- [x] CHANGELOG.md updated under [Unreleased].
- [x] Story-level self-audit checklist (docs/code-quality.md) completed (below).
- [x] Docs/ADRs updated if the design changed (no design change; N/A).

## Known issues

Resolved per maintainer ruling, following the same precedent as story-0003/
story-0005: small, noted deviations from the brief's verbatim text where the local
toolchain (MSVC + clang-tidy v22) demanded it. None change the consumed API surface
(`LogLevel`, `toString`, `LogSink`, `Logger`, `StderrSink`, `CapturingLogSink`) —
all signatures match the brief exactly.

1. **`clang-format` on the brief's verbatim bodies.** The brief's inline snippets
   (e.g. one-line `switch`/`case` bodies, brace-inline getters, a trailing same-line
   comment past the column limit) are not clang-format-clean under this repo's
   `.clang-format`. Fixed by running `clang-format -i` on every new file; output is
   whitespace-only, behavior unchanged.
2. **`misc-include-cleaner` (header hygiene, v22).** Added direct includes the
   brief's verbatim files relied on transitively: `revenant/core/log/LogLevel.hpp`
   to `Logger.hpp`, `StderrSink.hpp`, `CapturingLogSink.hpp`, and `LoggerTest.cpp`;
   `<string_view>` to `LogLevel.cpp` and `Logger.cpp`; `revenant/core/log/LogSink.hpp`
   to `Logger.cpp`. Same precedent as story-0003/story-0005.
3. **`modernize-use-designated-initializers`.** `Record{level, std::string{message}}`
   in `tests/support/CapturingLogSink.hpp` now uses `Record{.level = level, .message
   = std::string{message}}` (field order unchanged). Same pattern as story-0003's
   `Error{...}` fix.
4. **`cppcoreguidelines-pro-bounds-avoid-unchecked-container-access`.** In
   `LoggerTest.cpp`, `sink.records()[0]`/`[1]` now use `.at(0)`/`.at(1)` —
   bounds-checked, semantically identical for the in-range indices the test uses.

Verification transcript (`cmake --build --preset release --target tidy`, after
fixes 1–4 above):

```
[1/21] Processing file .../include/revenant/core/ByteReader.hpp.
...
[21/21] Processing file .../tests/unit/core/VersionTest.cpp.
100630 warnings generated.
Suppressed 100637 warnings (100630 in non-user code, 7 NOLINT).
```

Exit code 0 — no errors, all findings suppressed as non-user-code noise or existing
`NOLINT`s from earlier stories.

## Story-level self-audit (docs/code-quality.md)

- Responsibility & clarity: yes — every function does one thing (`toString`'s
  switch, `Logger::log`'s filter-then-forward, `StderrSink::write`'s one-line
  format-and-emit, `CapturingLogSink::write`'s record-and-store); names match
  behavior; each file has one responsibility (level enum + naming, sink interface,
  leveled front door, stderr sink, test-double sink).
- Design: `Logger` depends on the `LogSink` abstraction, never a concrete sink
  (DIP) — `StderrSink` and `CapturingLogSink` are interchangeable behind it, which
  is exactly what the tests exercise. `LogLevel`/`toString` have one reason to
  change (the set of levels); `Logger` has one reason to change (filtering/
  forwarding policy). No speculative abstraction: no formatting, no multi-sink
  fan-out, no severity-string customization beyond what the brief and tests need
  (YAGNI). No duplicated knowledge — `toString`'s switch is the single source of
  level names; `StderrSink` reuses it rather than re-encoding names.
- Anti-patterns: none introduced — no God object, no boolean-parameter traps, no
  nesting beyond 1 level, no dead or commented-out code. `LogSink` explicitly
  deletes copy/move to keep ownership unambiguous for an abstract base.
- Correctness & safety: `Logger::log` has one branch (below-threshold early
  return) and no swallowed errors — there is nothing fallible in this API surface
  (no I/O that can fail is performed by `Logger` itself; `StderrSink` writes to
  `std::cerr`, which is deliberately fire-and-forget per the brief). Read-only
  guarantee intact (no device I/O in this story). No UB: no raw pointer
  arithmetic, no casts; `Logger` holds a non-owning, never-null `LogSink*` for
  copyability, documented inline.
- Tests: written test-first (RED confirmed — build failed with `C1083: Cannot
  open include file: 'revenant/core/log/Logger.hpp'` — before any production file
  existed). Cover: filtered-below-threshold, forwarded-at-and-above-threshold
  (including message and level integrity across two calls), and every `LogLevel`
  enumerator's `toString`. `StderrSink` has no unit test asserting stderr
  contents (would be brittle to buffering/formatting details); per the brief it
  is exercised by Task 7's CLI — an accepted, explicitly scoped coverage gap for
  this story, not an oversight.
