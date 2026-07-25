<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M0 — Foundation & quality rig

**Goal:** turn the documented foundation into an executable, gated build. At the end of
M0 the repository compiles an (empty) `librevenant`, runs a test suite, and every CI
quality gate is active and green on Windows and Linux.

**Milestone:** [M0](../roadmap.md#m0--foundation--quality-rig)

## Outcome / definition of ready-to-close

- `cmake --preset debug && cmake --build --preset debug && ctest --preset debug` passes
  on both platforms.
- CI runs and passes: format-check, clang-tidy, file-length guard, duplication
  detector, ASan+UBSan tests, coverage plumbing.
- Core primitives (`Result<T>`, logging, byte views, endian readers) exist and are
  tested.
- `BlockDevice` + `ImageFileDevice` + `InMemoryDevice` exist and are tested.
- A synthetic-image generator exists in `tools/`.

## Stories

| Story | Title | Size |
|-------|-------|:----:|
| [story-0001](stories/story-0001-blockdevice-interface.md) | `BlockDevice` interface + `InMemoryDevice` | M |
| [story-0002](stories/story-0002-image-file-device.md) | `ImageFileDevice` (portable image reader) | M |
| [story-0003](stories/story-0003-result-and-byte-utilities.md) | `Result<T>`, endian readers, byte views | M |
| [story-0004](stories/story-0004-logging-facility.md) | Logging facility (leveled, testable sink) | S |
| [story-0005](stories/story-0005-cmake-lib-test-wiring.md) | CMake library/test wiring for `librevenant` | S |
| [story-0006](stories/story-0006-coverage-gate.md) | `check_coverage.py` + coverage gate at 85% | S |
| [story-0007](stories/story-0007-imagegen-scaffold.md) | Synthetic-image generator scaffold in `tools/` | M |
| [story-0057](stories/story-0057-ci-supply-chain-hardening.md) | CI supply-chain hardening: SHA-pin all actions, lock npm tools, add SECURITY.md, least-privilege tokens | S |

## Notes

- No filesystem or carving logic in M0 — only the foundation those layers stand on.
- The empty `librevenant` target exists so CI's build/test/coverage jobs activate.
