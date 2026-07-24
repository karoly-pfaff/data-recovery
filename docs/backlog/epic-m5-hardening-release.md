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
| story-0050 | Benchmark suite + baseline capture in CI | M |
| story-0051 | SIMD (AVX2) multi-pattern signature scan fast path | L |
| story-0052 | Multi-threaded range sharding for scans | M |
| story-0053 | Windows + Linux packaging (installers/archives) | M |
| story-0054 | User documentation & recovery playbook | M |
| story-0055 | 1.0.0 release checklist & tag | S |

## Notes

- Performance work is **measurement-gated**: no hand-tuned or assembly code lands
  without a benchmark proving the win. See [performance](../performance/strategy.md).
