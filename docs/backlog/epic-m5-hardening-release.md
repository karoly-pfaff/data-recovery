<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M5 — Hardening & 1.0 release

**Goal:** take the feature-complete toolkit to a polished, fast, well-packaged 1.0.0.

**Milestone:** [M5](../roadmap.md#m5--10-hardening)

## Outcome / definition of ready-to-close

- Signature scanning meets its throughput target on the benchmark suite, with the
  SIMD/hand-tuned fast path enabled behind a measured win (no regression gate green).
- Packages/installers exist for Windows and Linux.
- Complete user documentation (man pages / `--help`, usage guide, recovery playbook).
- `1.0.0` tagged, `CHANGELOG.md` finalized.

## Candidate stories (expanded when picked up)

| Story | Title | Size |
|-------|-------|:----:|
| story-0050 → | see [story-0050](stories/story-0050-benchmark-suite.md): the benchmark suite, and the gate that reads it | M |
| story-0051 | SIMD (AVX2) multi-pattern signature scan fast path | L |
| story-0052 | Multi-threaded range sharding for scans | M |
| story-0053 | Windows + Linux packaging (installers/archives) | M |
| story-0054 | User documentation & recovery playbook | M |
| story-0055 | 1.0.0 release checklist & tag | S |
| story-0056 | Move `fs/SafeArith.hpp` to a neutral home | S |

## Notes

- story-0056 came out of the M4 architecture audit. `fs::safeMul64`/`safeAdd64` are
  overflow-checked arithmetic over untrusted on-disk numbers, not filesystem knowledge,
  and `volume/` became their second caller during M4. The namespace is now a wart at
  those call sites; the fix is a move, not a redesign, so it waits for a quiet moment
  rather than widening a feature story.
- Performance work is **measurement-gated**: no hand-tuned or assembly code lands
  without a benchmark proving the win. See [performance](../performance/strategy.md).
