<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M2 — Carving breadth

**Goal:** widen the carve engine from JPEG to the full priority format set, each with a
validating parser, unit + fuzz tests, and the plausibility filter.

**Milestone:** [M2](../roadmap.md#m2--carving-breadth)

## Outcome / definition of ready-to-close

- Validating carvers exist for: PNG, MP4/MOV, RAW (CR2/NEF/ARW), ZIP-based
  (DOCX/XLSX/PPTX), PDF.
- Each is added via `revenant:add-format-carver` and passes its unit + fuzz gates.
- A format allowlist meaningfully reduces scan time and false positives.

## Candidate stories (expanded when picked up)

| Story | Title | Size |
|-------|-------|:----:|
| story-0020 → | see [story-0020](stories/story-0020-png-carver.md): PNG validating carver (chunk walk + CRC-32) | M |
| story-0021 → | see [story-0021](stories/story-0021-mp4-carver.md): MP4/MOV validating carver (box tree) | L |
| story-0022 → | see [story-0022](stories/story-0022-raw-carver.md): RAW carver (TIFF/IFD: CR2, NEF, ARW) | L |
| story-0023 → | see [story-0023](stories/story-0023-zip-carver.md): ZIP-based carver (EOCD; DOCX/XLSX/PPTX classification) | M |
| story-0024 → | see [story-0024](stories/story-0024-pdf-carver.md): PDF validating carver (`%PDF`…`%%EOF`, xref) | M |
| story-0025 | Plausibility filter + confidence reporting polish | S |

## Notes

- The formats are mutually independent — a strong candidate for parallel implementation
  (ultracode / worktree isolation), each in `src/carve/formats/`.
- No fragmentation handling in M2 (YAGNI); contiguous extents only.
