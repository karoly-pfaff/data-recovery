<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0010: JPEG validating carver

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: M

## Goal

Deliver the first `FormatCarver` and, with it, prove the validating-carving thesis end
to end: a JPEG is recovered at its **exact** extent by walking its marker structure —
never by collecting bytes to a size cap. This is the direct answer to the "1 GB SWF"
false positive.

## Design references

- [Carving engine](../../architecture/carving-engine.md)
- [ADR-0003: validating carving](../../architecture/adr/adr-0003-validating-carving.md)

## Signature & extent research (skill step 1)

- **Magic:** `FF D8 FF` at offset 0 (the SOI marker followed by the next marker's
  `0xFF` prefix). **Extension:** `jpg`.
- **Exact extent:** walk markers starting just past SOI.
  - Standalone markers carry no length payload: SOI `D8`, EOI `D9`, TEM `01`,
    RST0–7 `D0`–`D7`.
  - Every other marker is followed by a big-endian `u16` segment length that
    **includes its own two bytes**; `length < 2` is structurally invalid, and so
    is a declared length that would run past the available bytes
    (`pos + length > reader.size()`) — an over-declared length on truncated or
    hostile data is exactly the "grab bytes past validated structure" failure
    this carver exists to reject, so it folds into the same break handling as
    a too-short length.
  - After an SOS (`DA`) segment, entropy-coded data follows: `FF 00` is a stuffed
    data byte, `FF D0`–`D7` are restart markers (data continues after them), and
    any other `FF xx` terminates the entropy run and resumes the marker walk —
    this correctly handles progressive JPEGs' multiple SOS segments.
  - Repeated `0xFF` fill bytes immediately before a marker code are legal.
  - The file ends exactly one byte past `FF D9` (EOI); that offset is the
    returned length.
- **Verdicts:**
  - `kValid` — SOI matched, at least one SOS seen, EOI reached within bounds.
  - `kUncertain` — at least one SOS seen but the walk hit a structural break
    (bad marker prefix, segment length < 2 or past the buffer end) or the
    entropy scan ran out of bytes before EOI. Length is the **exact** position
    the walk reached before the break: entropy exhaustion is reported as a
    value (`EntropyOutcome{pos, foundTerminator=false}`), not an error, so
    every byte confirmed as entropy data (plain bytes, stuffed `FF 00`, RST
    pairs) is included and nothing past the last confirmed byte ever is.
  - `kRejected` — no SOI match, or the walk broke (bad marker prefix, segment
    length < 2 or past the buffer end) before any SOS was seen; length 0.
- **Totality (termination) argument:** every loop in the walk strictly advances
  its position each iteration — `skipFillBytes` only advances while it keeps
  seeing `0xFF` (bounded by the reader's remaining bytes), a marker segment
  always advances by its length (≥ 2, and now also checked ≤ the remaining
  bytes — see CRITICAL fix below — so it can never jump past the buffer), and
  each entropy step advances by at least 1 byte (plain data) or 2 bytes
  (stuffing/RST pair). Every read goes through `ByteReader`'s bounds-checked
  accessors; any out-of-range access becomes either a typed `Error` (a
  structural violation — bad prefix, an out-of-bounds segment length) or, for
  the entropy scan specifically, a plain `EntropyOutcome` value (exhaustion is
  an expected condition on truncated input, not an error) — either way the
  walk folds it into a truncation verdict (`kUncertain`/`kRejected`), never an
  unbounded loop or an out-of-bounds read. Because position is monotonically
  non-decreasing and strictly increases on every iteration that doesn't
  terminate, and the reader's size is finite, the walk always terminates.
- **Hand-trace of the minimal 22-byte fixture** (see
  `tests/unit/carve/formats/JpegCarverTest.cpp::minimalJpeg`): SOI (bytes
  0–1, pos→2) + APP0 segment `FF E0 00 04 4A 46` (marker 2 bytes + segment-length
  4 bytes incl. the length field itself, pos 2→8) + SOS segment `FF DA 00 02`
  (marker 2 bytes + segment-length 2 bytes, empty payload, pos 8→12) + entropy
  `01 FF 00 02` (plain byte, stuffed `FF 00`, plain byte; pos 12→16) + RST3
  continuation `FF D3` + data `03 04` (pos 16→20) + EOI `FF D9` (pos 20→22).
  End position 22 == fixture size (22 bytes); confirmed by both manual
  byte-offset arithmetic and by running the implementation.

## Acceptance criteria

- [x] `JpegCarver` implements `FormatCarver` in `src/carve/formats/JpegCarver.{hpp,cpp}`.
- [x] Signature: `FF D8 FF` (SOI). Extension: `jpg`.
- [x] `carve` walks markers from SOI, correctly skipping segments by their length and
      scanning entropy-coded data (handling `FF 00` byte-stuffing and RST markers), and
      returns the exact offset just past `FF D9` (EOI).
- [x] Returns `Valid` for a well-formed JPEG, `Uncertain` for a plausible-but-imperfect
      one, and `Rejected` for bytes that are not a JPEG.
- [x] Never reads past the provided reader bounds; never loops unboundedly on malformed
      input.
- [x] Registered in `CarverRegistry` (via `revenant::carve::registerBuiltinCarvers`,
      `include/revenant/carve/BuiltinCarvers.hpp` / `src/carve/BuiltinCarvers.cpp`).

## Test plan

- [x] Unit: minimal valid baseline JPEG → exact length, `Valid`.
- [x] Unit: JPEG followed by trailing garbage → extent stops at EOI (the core anti-false-
  positive test) — `JpegCarver.ExtentStopsAtEoiDespiteTrailingGarbage`.
- [x] Unit: truncated JPEG (no EOI) → bounded result, `Uncertain`, exact length (every
  confirmed entropy byte counted, nothing past it) —
  `JpegCarver.TruncatedAfterSosIsUncertainAndBounded`.
- [x] Unit: byte-stuffing and RST markers within entropy data handled correctly.
- [x] Unit: an over-declared segment length that would run past the buffer is rejected
  before any SOS (`Rejected`, length 0) and after SOS+entropy (`Uncertain`, exact
  length at the break) — `JpegCarver.OversizedSegmentLengthBeforeSosIsRejected` /
  `OversizedSegmentLengthAfterSosIsUncertainAndBounded` (review-round addition; the
  bounded version of the "1 GB SWF" false-positive class this story exists to kill).
- [x] Unit: a progressive-JPEG shape (SOS → entropy terminated by a non-EOI marker
  segment → second SOS → entropy → EOI) is `Valid` with the exact total length —
  `JpegCarver.TwoSosScansSeparatedByDhtAreValid` (review-round addition, pinning the
  multiple-SOS claim the design already made).
- [x] Golden-file: carve a JPEG embedded in a larger buffer; output is byte-identical to the
  original — `JpegCarveGolden.EmbeddedJpegIsRecoveredByteIdentical`.
- [x] **Fuzz:** libFuzzer target over `carve`; any input yields a verdict or typed error,
  never a crash, hang, or OOB read. (Merge gate.) — `tests/fuzz/JpegCarverFuzz.cpp`
  (CI-built; no local Clang toolchain in this environment).

## Definition of Done

- [x] Acceptance criteria met; unit + golden + fuzz tests green under ASan + UBSan
      (81/81 local `ctest --preset debug` pass; fuzz target builds in CI only).
- [x] `JpegCarver.cpp` ≤ 250 lines. The marker walk is factored into an internal
      `src/carve/formats/JpegMarkerWalk.{hpp,cpp}` pair, and the entropy-scan
      sub-problem is factored again into `JpegEntropyScan.{hpp,cpp}` (added in the
      review round once representing exhaustion as a value grew the combined file
      back over the line cap) — the same "internal, not public interface" pattern
      `WindowMatch.hpp` established for `SignatureScanner`. Final sizes: 65 + 199 +
      30 + 66 lines, all well under the 250-line hard cap; every function stays
      ≤ 8 statements / one thing (recognize, walk, segment skip, entropy scan, and
      verdict mapping are all separate functions).
- [x] Coverage held or raised; lint/format/duplication/file-length guards clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Story-level self-audit checklist completed (below).

## Story-level self-audit (docs/code-quality.md)

### Responsibility & clarity
- [x] Every new/changed function does exactly one thing at one abstraction level
      (recognize `startsWithSoi`; per-marker dispatch `applyMarker`; segment
      bounds-check `validatedSegmentEnd`/`skipSegment`/`applySegmentMarker`; entropy
      scan `scanEntropyData`/`advanceEntropyStep`/`classifyMarkerPair`; entropy-outcome
      folding `continueAfterEntropy`; fill-skip `skipFillBytes`; marker-code read
      `readMarkerPrefix`/`readCodeAfterPrefix`/`readMarkerCode`; verdict mapping
      `verdictFor`/`makeResult`; the outer loop `walkJpegMarkers`/`stepWalk`).
- [x] Each function's purpose is understood from its name and signature alone.
- [x] `JpegCarver.{hpp,cpp}` is focused on one responsibility (recognize + verdict
      mapping); `JpegMarkerWalk.{hpp,cpp}` on the marker-level walk (segments,
      standalone markers, EOI); `JpegEntropyScan.{hpp,cpp}` on exactly one other
      sub-problem (classifying entropy-coded bytes) — split progressively as each
      combined draft exceeded the 250-line file cap.

### Design
- [x] `Walk`/`JpegWalkOutcome` have one reason to change (mutable walk state vs.
      the reported outcome) — SRP.
- [x] Registration depends on the `FormatCarver` interface (DIP); `BuiltinCarvers`
      is the single, YAGNI-sized registration point for M1 (one carver today).
- [x] No duplicated *knowledge*: the unit and golden fixtures share the same byte
      sequence by design (independent failure domains — noted in both files), not
      by copy-pasted logic; jscpd only scans `src/include/tools`, so this does not
      trip the duplication gate.

### Anti-patterns
- [x] No God object, feature envy, boolean traps, or deep nesting introduced.
- [x] No premature generality, dead code, or commented-out code.

### Correctness & safety
- [x] Every error path is a typed `Result`/`Error` value; nothing swallowed.
- [x] Read-only: `carve()` only reads through `ByteReader`; no device write path.
- [x] No UB in byte handling: all reads go through `ByteReader::readLe`/`readBe`/
      `bytes` (bounds-checked); no `reinterpret_cast`, no unaligned deref.

### Tests
- [x] Written test-first (RED captured: missing-header compile failure before any
      implementation existed; see task report).
- [x] Tests cover malformed/edge inputs: non-JPEG bytes, truncation before and
      after SOS, a segment length below the 2-byte minimum, an over-declared
      segment length past the buffer (before and after SOS), fill bytes before a
      marker code, trailing garbage after a valid EOI, and a progressive-JPEG
      shape with two SOS scans.
- [x] `JpegCarver` has a dedicated fuzz target (`tests/fuzz/JpegCarverFuzz.cpp`).

## Notes

- `docs/architecture/carving-engine.md`'s format table already listed JPEG before this
  story started; no edit was needed there (skill step 7 is a no-op for this story).
- The marker-walk internals (`Walk`, the segment helpers, `walkJpegMarkers`) live in
  `src/carve/formats/JpegMarkerWalk.{hpp,cpp}`; the entropy-classification helpers
  (`EntropyOutcome`, `scanEntropyData`) live in a further internal split,
  `JpegEntropyScan.{hpp,cpp}` — both kept internal to `src/carve/formats/` the same
  way `WindowMatch.hpp` is internal to `src/carve/` for `SignatureScanner`. Split out
  once the single-file draft crossed the 250-line hard cap under clang-tidy's actual
  statement counts, and split again in the review round once representing entropy
  exhaustion as a value (rather than discarding it into an error) grew the file back
  past the cap (see task-2-report.md for the exact per-function counts and rationale).
- **Review round (post-initial-implementation):** a review of the first implementation
  found one Critical and two Important issues, all fixed on this branch (full detail,
  new hand-traces, and gate re-runs in task-2-report.md):
  1. **Critical — unbounded segment length.** `skipSegment` validated `length >= 2`
     but never checked the declared length against the remaining buffer, so a
     hostile/truncated segment could walk `pos` far past the actual data (up to
     65535 bytes of phantom extent) before the next read finally failed. Fixed by
     rejecting `pos + length > reader.size()` with the same typed error the
     too-short case already used.
  2. **Important — entropy exhaustion lost its position.** The original entropy
     scanner returned `Result<uint64_t>` and, on running out of bytes, returned
     `.error()` — discarding the position the scan had already reached and reporting
     a stale, earlier `walk.pos` (the position right after the SOS header) as the
     `Uncertain` length instead of the true break point. Restructured entropy
     scanning to return an `EntropyOutcome` value (`{pos, foundTerminator}`) so
     exhaustion is a value, not an error; the reported length is now **exact**.
  3. **Important — untested progressive-JPEG claim.** The design doc and code
     comments asserted multiple-SOS handling but no test exercised it; added one.
- Separately, three genuine bugs surfaced while implementing the originally drafted
  code (not just RED-state compile failures) and were corrected — recorded in full in
  `.superpowers/sdd/2026-07-26-m1-plan-a-carve-engine/task-2-report.md`: an internal
  header self-include using the wrong relative path, a GoogleTest macro/brace-init
  comma that MSVC's preprocessor mis-parses, and a `Result<T>` early-return returning
  the wrong error-carrying type. None change the documented signature/extent/verdict
  semantics above.
