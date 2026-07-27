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
- Core primitives: typed `Error`/`ErrorCode`, `Result<T>` (value-or-error with
  `map`), explicit-endianness integer readers (`fromLittleEndian`/`fromBigEndian`
  via `std::bit_cast`), and bounds-checked `ByteReader` with a libFuzzer target
  wired into CI's fuzz-smoke job.
- Leveled logging facility: `LogLevel`, injectable `LogSink` seam, `Logger`
  with threshold filtering, and a `StderrSink` for CLI tools.
- `BlockDevice`: the read-only, random-access I/O seam every layer reads
  through (no write operation exists on the interface), plus the
  `InMemoryDevice` test double backing unit tests.
- `ImageFileDevice`: portable read-only image reader (`.dd`/`.img`) with
  positioned, thread-safe reads (`pread` / overlapped `ReadFile`), typed
  `IoError`s carrying offset and OS code, platform code selected by CMake.
- Coverage gate: `tools/lint/check_coverage.py` enforces the 85% core-logic
  line-coverage floor from real llvm-cov data in CI (empty matches fail the
  gate); the checker itself is exercised by ctest fixture cases.
- `revenant-imagegen`: deterministic synthetic-image generator scaffold
  (zero / counter / LBA-tag patterns, exact byte sizes) — the seed of the
  test-image corpus; `toLittleEndian` added to the core endian helpers.
- Carve layer seams: `FormatCarver` interface, owning `CarverRegistry`, and
  the bounded-window streaming `SignatureScanner` that reports
  verdict-carrying candidates to a visitor (discovery only — extraction is
  deferred to arbitration per ADR-0006), plus the cross-layer `Confidence`
  verdict scale.
- JPEG validating carver: exact SOI→EOI extents via marker-structure walking
  (segment lengths bounds-checked against the input, byte-stuffing, restart
  markers, progressive JPEGs' multiple SOS scans) with
  Valid/Uncertain/Rejected verdicts, an exact `Uncertain` extent on truncated
  input (entropy exhaustion is a reported value, not a discarded error) —
  the validating-carving thesis proven end to end with golden-file
  byte-identity tests; registered via `registerBuiltinCarvers`.
- Output safety (ADR-0009): `sanitizeOutputPath` path-confinement choke point and the
  `boundedCount` allocation guard, both fuzz-tested.
- NTFS filename decoding (ADR-0010): lossless UTF-16→UTF-8 with escaping for
  undecodable units, plus deterministic output-name disambiguation.
- `PartitionView`: byte-range partition window over any `BlockDevice` — the fs
  layer's mount seam until real MBR/GPT parsing arrives (M4).
- `Result::andThen`: monadic bind for chaining `Result<T>` through error-preserving
  transformations (used by the NTFS boot-sector parser).
- `NtfsGeometry` + `parseBootSector`: validated NTFS boot-sector parser with
  per-field typed rejections, producing cluster size, MFT byte offset, and
  MFT record size.
- `MftRecordView` + `parseMftRecord`: validated NTFS MFT record parser with
  update-sequence fixup, attribute header parsing, resident/non-resident
  attribute distinction, and extraction of `$STANDARD_INFORMATION`,
  `$FILE_NAME`, and resident `$DATA` attributes. Non-resident `$DATA` runlist
  bytes are captured for the runlist decoder (story-0012).
- Seed corpora for the NTFS fuzz targets, generated reproducibly by
  `tools/fuzz/make_seed_corpus.py`. An empty corpus left the fuzz gate unable to
  reach past the `FILE`/`NTFS` magic within a short CI run.

### Changed
- `BootSector.cpp` split into the validation pipeline and `BootSectorFields.cpp`
  (per-field readers), keeping both well inside the file-length guard.
- C++ formatting convention: tab indentation (tabs for indent/continuation,
  spaces for alignment) and uniform parameter-list wrapping
  (`AlignAfterOpenBracket: AlwaysBreak` — no paren-column alignment);
  repo-wide mechanical reformat, recorded in `.git-blame-ignore-revs`.

### Fixed
- NTFS MFT attribute walker: an end-marker attribute (`0xFFFFFFFF`) positioned
  below `usedSize` stopped parsing without advancing the walk offset, so the
  caller re-read the same marker for ever. A crafted record hung the parsing
  thread outright, wedging a device scan rather than crashing it. Found by the
  seeded fuzz corpus.
- NTFS MFT attribute walker: a first-attribute offset within four bytes of the
  record end produced an out-of-range read whose typed error was discarded by an
  unchecked `Result::value()`, turning it into an escaping `std::bad_variant_access`
  instead of an uncertain record. Attribute type and header are now read as
  bounds-checked steps.
- NTFS update-sequence fixup: an array shorter than the record's 512-byte stride
  count was accepted, leaving the uncovered strides holding their on-disk USN
  placeholder while the record still graded valid. The count must now cover every
  stride.
- NTFS resident attribute bounds check: content offset and length were summed in
  the attribute's own 32-bit width, so a hostile length could wrap past the check
  it should fail. The sum is now widened before comparison.

### Security
- `SECURITY.md` policy: threat model for parsing hostile bytes, path-traversal and
  bounded-allocation guarantees, and private vulnerability reporting.
- CI hardening: least-privilege workflow token (`permissions: contents: read`), pinned
  GitHub Action and npm tool versions (removed floating `@latest`); full SHA-pinning
  tracked as story-0057.
- Frozen contract files: `.claude/settings.json` denies assistant `Edit`/`Write` on
  `AGENTS.md`/`CLAUDE.md`, and a versioned pre-commit hook (`.githooks/pre-commit`)
  rejects any commit touching them (defense in depth across all edit paths).
- CI supply chain pinned to immutable identities: all GitHub Actions by commit
  SHA, vcpkg by commit, jscpd via committed npm lockfile (`npm ci`), and
  checkouts no longer persist credentials.

[Unreleased]: https://example.invalid/revenant/compare/HEAD
