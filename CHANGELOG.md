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
- `decodeRunlist` + `runlistExtents`: NTFS `$DATA` runlist decoder. Data runs are
  walked structurally (nibble-encoded field widths, unsigned cluster counts,
  signed LCN deltas, sparse runs) with typed rejections for malformed widths,
  zero-length runs, deltas driving the LCN negative, cluster-total overflow, a
  missing end marker, and a run count past `kMaxDataRuns` (ADR-0009). Mapping to
  device byte extents is a separate, geometry-aware step that validates runs
  against the volume and trims the tail to the attribute's declared size —
  turning MFT metadata into the byte ranges a deleted file's content lives in.
  Sparse `$DATA` is decoded faithfully but refused by the extent mapper, so such
  a file goes to the carve pass rather than being reassembled wrongly.
- ZIP validating carver: the extent comes from the End Of Central Directory
  record, and the record is *checked* rather than merely found — a real archive
  satisfies `centralDirectoryOffset + size == eocdOffset` and has a directory
  header at that offset, so a stray `PK` in the data cannot end the
  file early. The last end record wins, since an archive may legitimately
  contain another archive's bytes. Office documents are ZIP archives, so the
  entry names name them: docx, xlsx, pptx, else zip. Fuzz-tested.
- Camera-RAW validating carver: RAW files are TIFF containers, so the extent
  comes from walking the IFD chain and taking the highest offset anything in the
  file points at — each IFD table, every out-of-line entry value, and the image
  data located by the strip or tile tag pairs, which is normally the file's last
  and largest part. Both byte orders are read in the file's own order; the IFD
  chain is capped because a `next` pointer may point backwards; the extension
  follows Canon's header marker or the `Make` tag (cr2/nef/arw/tif).
  Fuzz-tested.
- MP4/MOV validating carver: walks the top-level box list from `ftyp`, summing
  box sizes (both the 32-bit and the 64-bit `largesize` form) to the exact
  extent. A box size below its own header, a size running past the data, a
  size-0 "to end of file" box — whose extent a carve candidate cannot know — or
  a box type that is not four printable ASCII characters ends the walk as
  `Uncertain`; `ftyp` plus `moov` plus `mdat` is `Valid`. The `ftyp` major brand
  picks the extension, so QuickTime files come back as `.mov`. Fuzz-tested.
- PNG validating carver: walks the chunk list from the 8-byte signature through
  `IHDR` to `IEND`, verifying every chunk's CRC-32, and reports the exact extent.
  A failed CRC, a truncation, or a chunk length running past the data ends the
  trusted prefix as `Uncertain` instead of being waved through; bytes whose first
  chunk is not `IHDR` are `Rejected`. Fuzz-tested.
- `revenant::crc32`: IEEE 802.3 CRC-32 in core, pinned to the published check
  vectors — shared by the PNG chunk walk and (later) ZIP entries.
- NTFS synthetic-image builder in `tools/imagegen`: a fixed 4 MiB fixture volume
  with a real boot sector, a 32-record `$MFT`, and a directory tree holding live,
  deleted, and orphaned files — including a **fragmented** deleted JPEG, a
  deleted file with resident `$DATA`, and one JPEG in unallocated space that no
  record points at. Built from small single-purpose units (`NtfsLayout`,
  `BootSectorBuilder`, `RunlistEncoder` — the inverse of the story-0012 decoder —
  `AttributeBuilder`, `MftRecordBuilder`, `NtfsImageBuilder`), each specified
  against the production parser that reads it back. The integration test walks
  the generated image through `ImageFileDevice` → `parseBootSector` →
  `parseMftRecord` → `decodeRunlist` → `runlistExtents` and recovers every
  file's bytes identically, which is the M1 vertical slice proven end to end on
  real metadata.
- Seed corpora for the NTFS fuzz targets, generated reproducibly by
  `tools/fuzz/make_seed_corpus.py`. An empty corpus left the fuzz gate unable to
  reach past the `FILE`/`NTFS` magic within a short CI run.

### Changed
- `revenant-imagegen` now takes a subcommand: `pattern <output> <size> <name>`
  (the story-0007 behaviour) or `ntfs <output>`. The verb-less form is gone —
  a developer tool with no external consumers is better renamed than left with
  two silently overlapping grammars.
- `BootSector.cpp` split into the validation pipeline and `BootSectorFields.cpp`
  (per-field readers), keeping both well inside the file-length guard.
- C++ formatting convention: tab indentation (tabs for indent/continuation,
  spaces for alignment) and uniform parameter-list wrapping
  (`AlignAfterOpenBracket: AlwaysBreak` — no paren-column alignment);
  repo-wide mechanical reformat, recorded in `.git-blame-ignore-revs`.

### Fixed
- Signature scanner: a magic sitting closer to the device start than its own
  in-file offset wrapped the unsigned `windowOffset + at - signature.offset`
  subtraction and invented a candidate near the end of the address space. Latent
  while every signature sat at offset 0; MP4's `ftyp` at offset 4 is the first
  that could reach it. The candidate start now goes through a checked helper,
  and a window travels with its device offset as one value.
- `sanitizeOutputPath` rejected every legitimate name when the output root was
  reached through a filesystem alias — a symlink or junction, or a Windows 8.3
  short name such as `C:\RECOVE~1`. Containment compared the assembled path,
  which keeps the caller's spelling, against a canonicalized root: two different
  namings of the same directory. Both sides are canonicalized now; the returned
  path still uses the caller's spelling. This is why the Windows CI job had been
  red since the guard landed — GitHub's temp directory arrives as `RUNNER~1`.
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
