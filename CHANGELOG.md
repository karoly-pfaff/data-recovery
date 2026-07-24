<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Changelog

All notable changes to Revenant are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
See [`docs/versioning.md`](docs/versioning.md).

## [Unreleased]

### Added
- Project foundation: engineering contract (`AGENTS.md`), agent guide (`CLAUDE.md`),
  and repository documentation set under `docs/`.
- Architecture documentation: layered design, I/O abstraction, filesystem parsers,
  carving engine, and hybrid orchestration, plus initial ADRs.
- Roadmap (M0–M5) and backlog structure (epics + stories).
- Testing strategy and quality-gate definitions; versioning, code-quality, and
  performance standards.
- Toolchain configuration: CMake + vcpkg, CMake presets, `.clang-format`,
  `.clang-tidy`, `.editorconfig`, and CI workflow (Windows + Linux, sanitizers,
  coverage, duplication detection, file-length guard, fuzz smoke).
- `revenant:add-format-carver` project skill for adding new carve formats.

[Unreleased]: https://example.invalid/revenant/compare/HEAD
