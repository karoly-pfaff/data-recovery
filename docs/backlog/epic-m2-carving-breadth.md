<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M2 — Carving breadth

**Status: complete.** Every story below is merged; the outcome checklist holds.

**Goal:** widen the carve engine from JPEG to the full priority format set, each with a
validating parser, unit + fuzz tests, and the plausibility filter.

**Milestone:** [M2](../roadmap.md#m2--carving-breadth)

## Outcome / definition of ready-to-close

- Validating carvers exist for: PNG, MP4/MOV, RAW (CR2/NEF/ARW), ZIP-based
  (DOCX/XLSX/PPTX), PDF.
- Each is added via `add-format-carver` and passes its unit + fuzz gates.
- A format allowlist meaningfully reduces scan time and false positives.

## Candidate stories (expanded when picked up)

| Story | Title | Size |
|-------|-------|:----:|
| story-0201 → | see [story-0201](stories/story-0201-png-carver.md): PNG validating carver (chunk walk + CRC-32) | M |
| story-0202 → | see [story-0202](stories/story-0202-mp4-carver.md): MP4/MOV validating carver (box tree) | L |
| story-0203 → | see [story-0203](stories/story-0203-raw-carver.md): RAW carver (TIFF/IFD: CR2, NEF, ARW) | L |
| story-0204 → | see [story-0204](stories/story-0204-zip-carver.md): ZIP-based carver (EOCD; DOCX/XLSX/PPTX classification) | M |
| story-0205 → | see [story-0205](stories/story-0205-pdf-carver.md): PDF validating carver (`%PDF`…`%%EOF`, xref) | M |
| story-0206 → | see [story-0206](stories/story-0206-plausibility-filter.md): Plausibility filter + format allowlist | S |

## Notes

- The formats are mutually independent — a strong candidate for parallel implementation
  (ultracode / worktree isolation), each in `src/carve/formats/`.
- No fragmentation handling in M2 (YAGNI); contiguous extents only.
