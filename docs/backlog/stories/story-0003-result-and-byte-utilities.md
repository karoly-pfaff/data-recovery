<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0003: `Result<T>`, endian readers, byte views

- Epic: [epic-m0-foundation](../epic-m0-foundation.md)
- Status: Done
- Size: M

## Goal

Provide the core primitives every other layer relies on: a typed error-or-value
`Result<T>`, safe byte-view reading, and explicit-endianness integer readers. These make
the "errors are values" and "no UB in byte code" rules mechanically easy to follow.

## Design references

- [Architecture overview → cross-cutting concerns](../../architecture/overview.md)
- [ADR-0004: toolchain](../../architecture/adr/adr-0004-toolchain-cmake-vcpkg-cpp20.md)

## Acceptance criteria

- [x] `Result<T>` carries either a value or a typed `Error`; querying the wrong
      alternative is a defined, testable error, never UB.
- [x] A `ByteReader` wraps `std::span<const std::byte>` and offers bounds-checked reads;
      an out-of-range read yields a typed error.
- [x] `readLe<T>` / `readBe<T>` read fixed-width integers with explicit endianness via
      `std::bit_cast` (no `reinterpret_cast`, no unaligned deref).
- [x] No integer read can overrun the underlying span.

## Test plan

- [x] Unit: `Result<T>` value/error construction, mapping, and misuse are all tested.
- [x] Unit: little- and big-endian reads of 8/16/32/64-bit values against known bytes.
- [x] Unit: reads at and beyond the span boundary return typed errors.
- [x] Fuzz (smoke): `ByteReaderFuzz` feeds random spans/offsets to `ByteReader`; wired
      into CI's `fuzz-smoke` job (Linux/Clang only — see "Known issues").

## Definition of Done

- [x] Acceptance criteria met; tests green under ASan + UBSan (21/21,
      `ctest --preset debug`).
- [x] No `reinterpret_cast` in byte code; verified by clang-tidy
      (`cmake --build --preset release --target tidy` clean).
- [x] Coverage held or raised; all lint/guard gates clean (guard-limits,
      format-check, tidy all pass; no local coverage-preset run this story —
      same scope limitation as story-0005).
- [x] `CHANGELOG.md` updated; story-level self-audit completed (below).

## Known issues

Resolved per maintainer ruling, mirroring story-0005's precedent of small,
noted deviations from the brief's verbatim text where the local toolchain
(MSVC + clang-tidy v22) demanded it. None change any consumed API surface
(`Error`, `Result<T>`, `fromLittleEndian`/`fromBigEndian`, `ByteReader`) —
all signatures match the brief exactly.

1. **MSVC C4333 on `Endian.hpp`'s `byteSwap`.** For `T = uint8_t`, `value >>
   8U` shifts by the type's own width; MSVC's `/WX` flags this as data loss
   even though the promoted-`int` shift is well-defined and the result is
   never read again. Fix: guard the shift with `if constexpr (sizeof(T) >
   1)`, which discards it from that one instantiation at compile time.
   Output is unchanged for every `T`.
2. **`readability-identifier-naming` on local `const` locals.** The
   project's `.clang-tidy` set `ConstantCase`/`ConstantPrefix` (for named
   constants like `kSectorSize`) but never `LocalConstantCase`, so ordinary
   local `const` variables (e.g. `const Result<int> result = ...` — and the
   brief's own `Endian.hpp` `const T native = ...`) fell back to the
   `k`-prefixed `Constant` style. Fix: added
   `readability-identifier-naming.LocalConstantCase: camelBack` to
   `.clang-tidy`, matching `LocalVariableCase` and AGENTS.md's plain
   "variables, parameters → camelCase" rule. This is a config-gap fix, not a
   file-content deviation.
3. **`misc-include-cleaner` (header hygiene, v22).** Added direct includes
   the brief's verbatim files relied on transitively: `<cstddef>`,
   `<cstdint>`, `<span>`, `revenant/core/Error.hpp`, and
   `revenant/core/Result.hpp` to `src/core/ByteReader.cpp`; `<span>` to
   `EndianTest.cpp`; `revenant/core/Error.hpp` to `ResultTest.cpp` and
   `ByteReaderTest.cpp`. Same precedent as story-0005's `Version.cpp` fix.
4. **`modernize-use-designated-initializers`.** `Error{ErrorCode::kX, n}`
   aggregate-inits of named variables now use `Error{.code = ErrorCode::kX,
   .offset = n}` in `src/core/ByteReader.cpp` and `ResultTest.cpp` (field
   order unchanged: `code`, `offset`, `osCode`). Semantically identical.
5. **Fuzz harness gate failures (`tests/fuzz/ByteReaderFuzz.cpp`).** Three
   real findings, not tooling quirks:
   - `readability-function-size`: the brief's `LLVMFuzzerTestOneInput` body
     was 11 statements against the project's hard 10-statement limit. Fixed
     by extracting a `toByteVector(std::span<const std::uint8_t>)` helper
     (also removes the inline lambda from the entry point's scope), bringing
     it back under the limit without changing the fuzzing behavior.
   - `cppcoreguidelines-pro-bounds-pointer-arithmetic` on `data + size`:
     replaced with `std::span<const std::uint8_t>{data, size}` passed to the
     new helper, using span iterators instead of raw pointer arithmetic —
     directly in the spirit of AGENTS.md §3's "no UB in byte code; use
     `std::span`" rule.
   - `readability-identifier-naming` on `LLVMFuzzerTestOneInput` itself:
     this is libFuzzer's fixed C-ABI entry-point name, not renameable.
     Suppressed with a single-site `NOLINTNEXTLINE` and a comment, per
     AGENTS.md's "suppression allowed with inline justification" rule —
     same pattern already used for `Result`'s `NOLINT(google-explicit-constructor)`.
   - Also applied `std::ranges::transform` (over the iterator-pair overload)
     in the new helper per `modernize-use-ranges`, consistent with
     `Endian.hpp`'s existing `std::ranges::copy` usage.

## Story-level self-audit (docs/code-quality.md)

- Responsibility & clarity: yes — every function does one thing
  (`Result::map`, `ByteReader::bytes`, `fromLittleEndian`/`fromBigEndian`,
  `detail::byteSwap`/`nativeFromBytes`, the fuzz harness's `toByteVector`/
  `exercise` split); names match behavior; each file has one responsibility
  (error type, value-or-error, endian conversion, bounds-checked reading).
- Design: `Result<T>` and `ByteReader` have one reason to change each (SRP);
  `ByteReader` depends on `Result`/`Endian`, not the reverse (DIP holds at
  this layer — no concrete I/O dependency yet); no speculative abstraction
  (YAGNI — `map` is the only combinator, added because the brief/tests need
  it); no duplicated knowledge beyond the intentional `readLe`/`readBe`
  symmetry, which mirrors the `fromLittleEndian`/`fromBigEndian` pair it
  wraps.
- Anti-patterns: none introduced — no God object, no boolean-parameter
  traps, no nesting beyond 2 levels, no dead or commented-out code.
- Correctness & safety: all error paths are typed `Result`/`Error` values,
  nothing swallowed; read-only guarantee intact (no device I/O in this
  story); byte handling is UB-free (`std::span`, `std::bit_cast`, explicit
  bounds checks in `ByteReader::bytes`, no `reinterpret_cast`).
- Tests: written test-first (RED confirmed for each of the three units
  before implementation); cover value/error/misuse for `Result`, all four
  widths x2 endiannesses for `Endian`, and in-range/past-end/zero-count/huge-
  offset/integer-overrun edge cases for `ByteReader`; the fuzz target
  exercises the same `ByteReader` API against arbitrary bytes/offsets.
