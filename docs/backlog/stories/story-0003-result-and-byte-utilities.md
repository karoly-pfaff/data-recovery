<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0003: `Result<T>`, endian readers, byte views

- Epic: [epic-m0-foundation](../epic-m0-foundation.md)
- Status: Ready
- Size: M

## Goal

Provide the core primitives every other layer relies on: a typed error-or-value
`Result<T>`, safe byte-view reading, and explicit-endianness integer readers. These make
the "errors are values" and "no UB in byte code" rules mechanically easy to follow.

## Design references

- [Architecture overview → cross-cutting concerns](../../architecture/overview.md)
- [ADR-0004: toolchain](../../architecture/adr/adr-0004-toolchain-cmake-vcpkg-cpp20.md)

## Acceptance criteria

- [ ] `Result<T>` carries either a value or a typed `Error`; querying the wrong
      alternative is a defined, testable error, never UB.
- [ ] A `ByteReader` wraps `std::span<const std::byte>` and offers bounds-checked reads;
      an out-of-range read yields a typed error.
- [ ] `readLe<T>` / `readBe<T>` read fixed-width integers with explicit endianness via
      `std::bit_cast` (no `reinterpret_cast`, no unaligned deref).
- [ ] No integer read can overrun the underlying span.

## Test plan

- Unit: `Result<T>` value/error construction, mapping, and misuse are all tested.
- Unit: little- and big-endian reads of 8/16/32/64-bit values against known bytes.
- Unit: reads at and beyond the span boundary return typed errors.
- Fuzz (smoke): feed random spans/offsets to `ByteReader`; it must never crash.

## Definition of Done

- [ ] Acceptance criteria met; tests green under ASan + UBSan.
- [ ] No `reinterpret_cast` in byte code; verified by clang-tidy.
- [ ] Coverage held or raised; all lint/guard gates clean.
- [ ] `CHANGELOG.md` updated; story-level self-audit completed.
