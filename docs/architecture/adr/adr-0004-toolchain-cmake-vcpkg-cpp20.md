<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0004: Toolchain — CMake + vcpkg, C++20, GoogleTest

- **Status:** Accepted
- **Date:** 2026-07-24

## Context

The project targets Windows and Linux, needs reproducible dependency management, and
must support strong quality tooling (clang-tidy, clang-format, sanitizers, coverage,
fuzzing). The language choice must balance modern, safety-improving features against
broad, stable compiler support for low-level byte manipulation.

## Decision

- **Build system:** CMake (≥ 3.25) with presets, the de-facto cross-platform standard
  with first-class IDE and CI support.
- **Dependencies:** vcpkg in manifest mode (`vcpkg.json`) for reproducible builds.
- **Language:** C++20. It provides `concepts`, `ranges`, `std::span`, `std::bit_cast`,
  and `std::format` — directly useful for safe byte handling — while enjoying mature
  support on MSVC 2022, GCC 13+, and Clang 16+.
- **Test framework:** GoogleTest + GoogleMock, the enterprise standard with rich
  assertions, mocking, and death tests.

## Consequences

- A single, portable build description across both platforms.
- `std::span` and `std::bit_cast` let us forbid `reinterpret_cast`-based aliasing and
  unaligned dereferences in byte code, supporting the no-UB rule.
- C++23-only conveniences (`std::expected`, `std::stacktrace`) are unavailable; we
  provide a small in-house `Result<T>` instead. Revisit if we raise the baseline.
- Contributors need a vcpkg checkout and a recent compiler; documented in `CLAUDE.md`.
