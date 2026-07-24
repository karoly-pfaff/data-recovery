<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Carving Engine

The carving engine finds files in raw byte ranges without relying on filesystem
metadata. It is what powers `revenant-carve` and the unallocated-space pass of the
hybrid `revenant-undelete`. Its defining property is **validation**: every candidate is
parsed to its exact extent and checked before it becomes a recovered file.

## Why validating carving

A magic byte is a *hypothesis*, not a file. The engine treats a header match as the
start of a validation attempt:

1. **Recognize** — a signature matches at some offset.
2. **Measure** — the format's parser walks its internal structure to compute the exact
   end offset. No "read until the next header" and no fixed size cap standing in for a
   real length.
3. **Validate** — structural invariants must hold (chunk CRCs, box nesting, marker
   sequence, declared-vs-actual sizes). A plausibility verdict is attached.
4. **Emit or reject** — valid extents are handed to the sink; invalid ones are dropped
   or flagged per configuration.

This is the mechanism that prevents the "1 GB SWF" class of false positive. See
[ADR-0003](adr/adr-0003-validating-carving.md).

## The `FormatCarver` interface

```cpp
namespace revenant::carve {

struct Signature {
    std::span<const std::byte> magic; // bytes to match
    std::size_t offset;               // where in the file the magic sits (usually 0)
};

enum class Confidence { Rejected, Uncertain, Valid };

struct CarveResult {
    std::uint64_t length;     // exact extent in bytes
    Confidence confidence;    // validation verdict
    std::string extension;    // e.g. "jpg"
};

class FormatCarver {
public:
    virtual ~FormatCarver() = default;

    // The signatures that trigger a validation attempt for this format.
    [[nodiscard]] virtual std::span<const Signature> signatures() const = 0;

    // Given a reader positioned at a candidate header, walk the structure and
    // return the exact extent + verdict, or a rejection.
    [[nodiscard]] virtual Result<CarveResult> carve(ByteReader& reader) const = 0;
};

} // namespace revenant::carve
```

A `FormatCarver` is small and single-purpose: one format, one file, roughly
`signatures()` + `carve()`. Anything larger is a smell (Prime Directive). New carvers
are added with the [`revenant:add-format-carver`](../../skills/add-format-carver/SKILL.md)
skill, which scaffolds the parser plus its mandatory unit and fuzz tests.

## Registry and scanning

- **`CarverRegistry`** holds all registered carvers and builds a combined matcher over
  their signatures.
- **Signature scanning** streams the device in bounded windows and reports every offset
  where any signature matches. The hot inner loop is a multi-pattern byte search
  (Aho-Corasick-style), with an optional SIMD fast-path
  ([performance strategy](../performance/strategy.md)) — the natural home for the
  project's hand-tuned/assembly work, gated behind benchmarks.
- On a match, the corresponding carver's `carve()` runs against a reader positioned at
  the offset. A `Valid`/`Uncertain` result becomes a **candidate** (not an emitted file
  — see arbitration below); scanning resumes past the candidate extent to avoid
  re-parsing the same bytes.

## Candidate index & arbitration

Discovery is separated from extraction. A carver match never writes a file directly —
that eager behaviour is exactly the PhotoRec failure we reject. Instead
([ADR-0006](adr/adr-0006-candidate-arbitration-deferred-extraction.md)):

1. **Discover.** Each `carve()` verdict produces a *candidate*
   `{region (offset, length), format, confidence, source}`, appended to a **file-backed
   candidate index** (persistent, not in-memory — this must scale to terabyte devices).
2. **Arbitrate.** Candidates competing for overlapping regions are resolved by
   confidence: a higher-confidence candidate suppresses lower-confidence ones on the same
   region. Filesystem-recovered entries enter the index as high-confidence primaries, so
   they automatically win over carve candidates on their regions (the hybrid synergy).
3. **Extract.** Only winning candidates are materialized to the sink. A weak secondary
   match (e.g. a spurious "SWF") is written **only if no primary covers its region**.

Tie-breaking and partial-overlap policy live in `recovery/`, not in carvers. A carver's
responsibility ends at returning a verdict.

## Per-format validation (initial set)

| Format        | How the exact extent is determined                                    |
|---------------|------------------------------------------------------------------------|
| JPEG          | Walk markers SOI → … → EOI, tracking entropy-coded segments.           |
| PNG           | Walk the chunk list IHDR → … → IEND, verifying each chunk's CRC-32.    |
| MP4 / MOV     | Walk the atom/box tree (`ftyp`, `moov`, `mdat`), summing box sizes.    |
| RAW (CR2/NEF/ARW) | Parse the TIFF header and IFD chain.                               |
| ZIP-based (DOCX/XLSX/PPTX) | Locate the End-Of-Central-Directory record; derive extent. |
| PDF           | Match `%PDF` … final `%%EOF`, validating the xref/trailer.             |

Each row is one `FormatCarver` with its own tests. Formats beyond the initial set are
added incrementally per the [roadmap](../roadmap.md).

## Fragmentation (forward-looking)

The initial engine assumes contiguous files (as PhotoRec does by default), which covers
the overwhelming majority of real recoveries on modern media. Fragmentation-aware
reassembly is a deliberately deferred concern with its own future ADR — we do not build
speculative machinery for it now (YAGNI).

## Threat model & testing

The engine parses **hostile, corrupt bytes by definition** — that is the whole job.
Therefore:

- Every carver has unit tests with valid, truncated, and malformed fixtures.
- Every carver has a **libFuzzer target**; fuzzing is a merge gate, not optional.
- Parsers must be total: any input yields a verdict or a typed error, never a crash,
  hang, or out-of-bounds read.
