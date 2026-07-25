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
- Trunk-based git workflow (`docs/git-workflow.md`): one `story/*` branch per story,
  squash-merged to `main`; epics/milestones as labels, not branches.
- Design decisions captured as ADRs 0006–0010: candidate arbitration & deferred
  extraction, block-level (incl. network) access boundary, resumable checkpointing,
  output safety (path confinement + bounded allocation), and filename decoding.
- `docs/architecture/recovery-output.md`: session manifest (provenance + SHA-256 +
  bad-sector map), `--dry-run` preview, and destination scaling.
- Backlog stories for output safety, filename decoding, manifest, dry-run, resumable
  scan (M1), plus imaging mode and network block device (M4).
- Build activation: static `librevenant` with `revenant::version()`, GoogleTest
  test target (`revenant_tests`), and working CI build/test jobs on Windows and
  Linux (MSVC dev env, vcpkg bootstrap, MSVC-ASan `/RTC1` fix, coverage test
  preset, duplication gate made enforcing).

### Security
- `SECURITY.md` policy: threat model for parsing hostile bytes, path-traversal and
  bounded-allocation guarantees, and private vulnerability reporting.
- CI hardening: least-privilege workflow token (`permissions: contents: read`), pinned
  GitHub Action and npm tool versions (removed floating `@latest`); full SHA-pinning
  tracked as story-0057.
- Frozen contract files: `.claude/settings.json` denies assistant `Edit`/`Write` on
  `AGENTS.md`/`CLAUDE.md`, and a versioned pre-commit hook (`.githooks/pre-commit`)
  rejects any commit touching them (defense in depth across all edit paths).

[Unreleased]: https://example.invalid/revenant/compare/HEAD
