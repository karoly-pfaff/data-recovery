<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0001: `BlockDevice` interface + `InMemoryDevice`

- Epic: [epic-m0-foundation](../epic-m0-foundation.md)
- Status: Done
- Size: M

## Goal

Establish the single seam through which all byte sources are read. Define the
`BlockDevice` interface and provide `InMemoryDevice`, the in-memory implementation that
backs the majority of unit tests. Everything above the I/O layer depends on this
abstraction, never on a concrete device.

## Design references

- [I/O layer](../../architecture/io-layer.md)
- [ADR-0005: read-only by default](../../architecture/adr/adr-0005-read-only-by-default.md)

## Acceptance criteria

- [x] `BlockDevice` is declared in `include/revenant/core/io/BlockDevice.hpp` with
      `sizeInBytes()`, `sectorSize()`, and `readAt(offset, span) -> Result<size_t>`.
- [x] The interface exposes no write operation.
- [x] `InMemoryDevice` (in `tests/`) is constructed from a byte buffer and a configurable
      sector size, and implements `BlockDevice`.
- [x] `readAt` beyond end-of-device returns a short read (value), not an error.
- [x] `readAt` with a zero-size span returns 0 and reads nothing.

## Test plan

- [x] Unit: full read; partial read at the tail; read entirely past the end (0 bytes);
      zero-length read; offset + size overflow is handled as a typed error, not UB.
- [x] Unit: `sectorSize()` is reported as constructed.
- [x] The interface is used through a `BlockDevice&` in tests to confirm it is a usable seam.

## Definition of Done

- [x] Acceptance criteria met; tests green under ASan + UBSan (30/30,
      `ctest --preset debug`, Windows dev machine — Linux covered by CI, not
      run locally this story; same scope limitation as story-0003/story-0005).
- [x] Coverage held or raised; all lint/guard gates clean (guard-limits,
      format-check, tidy all pass; no local coverage-preset run this story —
      same scope limitation as story-0003/story-0005).
- [x] clang-format, clang-tidy, duplication (`jscpd`, 0 clones), and
      file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Story-level self-audit checklist ([code-quality](../../code-quality.md)) completed.
- [x] Each file ≤ 250 lines; each function ≤ 10 statements, does one thing.

## Known issues

Mirrors story-0003's precedent of small, noted, behavior-preserving deviations from the
brief's verbatim text where the local toolchain (MSVC + clang-tidy v22) demanded it. None
change the `BlockDevice`/`InMemoryDevice` API surface — every signature matches the brief
exactly.

1. **`misc-include-cleaner` (header hygiene, v22).** Added direct includes the brief's
   verbatim files relied on transitively: `revenant/core/Result.hpp` to
   `tests/support/InMemoryDevice.hpp`; `<cstddef>`, `<cstdint>`, `<span>`, `<vector>`,
   `revenant/core/Error.hpp`, `revenant/core/Result.hpp` to
   `tests/support/InMemoryDevice.cpp`; `revenant/core/Error.hpp` and
   `revenant/core/io/BlockDevice.hpp` to `tests/unit/io/InMemoryDeviceTest.cpp`.
2. **`modernize-use-designated-initializers`.** `Error{ErrorCode::kOverflow, offset}` in
   `InMemoryDevice::readAt` now uses `Error{.code = ErrorCode::kOverflow, .offset =
   offset}` (field order unchanged). Semantically identical.
3. **`cppcoreguidelines-pro-bounds-avoid-unchecked-container-access`.** `bytes[i]` /
   `bytes[63]` in `InMemoryDeviceTest.cpp`'s `patternBytes` helper and
   `FullReadThroughInterface` now use `bytes.at(i)` / `bytes.at(63)`. Both accesses are
   already range-guarded by construction/an `ASSERT_EQ` on size, so behavior is
   unchanged; `.at()` only adds a redundant, never-taken throw path.
4. **`bugprone-easily-swappable-parameters` on the test-only `readThroughInterface`
   helper.** Not one of the three pre-authorized accommodation categories, so flagged
   here explicitly as a judgment call: `(std::uint64_t offset, std::size_t count)`
   mirrors `BlockDevice::readAt`'s own parameter order, both names are descriptive, and
   introducing wrapper types for a private test helper was judged out of scope for this
   story. Suppressed with a single-site `NOLINTNEXTLINE` and a comment, same pattern as
   `Result`'s existing `NOLINT(google-explicit-constructor)` and story-0003's fuzz-harness
   suppression. Note: this same `(offset, count)` shape appears unflagged in
   `ByteReader::bytes` — the check's swap-risk heuristic did not fire there, only on the
   3-parameter `readThroughInterface`; not investigated further since either way the
   resolution (a justified `NOLINT`) would be the same.
5. **`clang-format` line-wrapping.** The brief's hand-wrapped continuation lines (e.g.
   `readAt`'s parameter list, `readThroughInterface`'s signature) didn't match the
   project's 100-column/`BinPackParameters: false` style; reformatted in place with
   `clang-format -i`. Purely whitespace — no semantic change.

## Story-level self-audit (docs/code-quality.md)

- Responsibility & clarity: yes — `BlockDevice` declares the seam only (no logic);
  `InMemoryDevice`'s four methods each do one thing (construct, report size, report
  sector size, bounds-checked copy); names match behavior; each file has one
  responsibility (interface, test-double declaration, test-double implementation, tests).
- Design: `InMemoryDevice` has one reason to change (SRP); the test
  (`readThroughInterface`, `InMemoryDeviceTest.cpp`) depends only on `BlockDevice&`, never
  on the concrete `InMemoryDevice` methods beyond construction (DIP holds — this is the
  story's whole point); no speculative abstraction (YAGNI — no decorators, no write path,
  no factory yet; those are separate stories); no duplicated knowledge.
- Anti-patterns: none introduced — no God object, no boolean-parameter traps, no nesting
  beyond 2 levels, no dead or commented-out code.
- Correctness & safety: the only error path (`kOverflow`) is a typed `Result`/`Error`
  value, nothing swallowed; the source-device read-only guarantee is structural here —
  `BlockDevice` has no write member at all; byte handling is UB-free (`std::span`,
  explicit overflow check before any arithmetic, `std::copy_n` over iterators, no
  `reinterpret_cast`).
- Tests: written test-first (RED confirmed — CMake configure failed on the missing
  `support/InMemoryDevice.cpp`/headers before implementation existed); cover full read,
  short read at the tail, entirely-past-end, zero-length, offset+size overflow, and
  constructed-geometry reporting — the exact edge set the acceptance criteria call out.
  `InMemoryDevice` is a test double, not a byte-parser, so no fuzz target applies.

## Concerns for follow-up stories

- `InMemoryDevice::readAt` is not currently exercised with a `sectorSize` other than
  512/4096 or with unaligned offsets crossing the "native sector size" boundary — fine
  per the interface contract ("reads need not be sector-aligned"), just noting for
  `PhysicalDevice`/`ImageFileDevice` (story-0002+) where alignment becomes load-bearing.
