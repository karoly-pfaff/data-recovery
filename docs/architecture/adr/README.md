<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Architecture decision records

Each file records one decision: what was chosen, what it rules out, and why. They are
the authority for the decisions they name — where a guide or a comment restates one, the
ADR wins. Superseding a decision means a new record, not an edit to the old one
([ADR-0001](adr-0001-record-architecture-decisions.md)).

| ADR | Decision |
|-----|----------|
| [0001](adr-0001-record-architecture-decisions.md) | Record architecture decisions |
| [0002](adr-0002-two-frontends-shared-core.md) | Two frontends over a shared core |
| [0003](adr-0003-validating-carving.md) | Structure-aware, validating carving — precision over recall |
| [0004](adr-0004-toolchain-cmake-vcpkg-cpp20.md) | Toolchain: CMake + vcpkg, C++20, GoogleTest |
| [0005](adr-0005-read-only-by-default.md) | **The source device is never written** |
| [0006](adr-0006-candidate-arbitration-deferred-extraction.md) | Candidate arbitration and deferred extraction |
| [0007](adr-0007-block-level-access-boundary.md) | Block-level access boundary, including network sources |
| [0008](adr-0008-resumability-checkpointing.md) | Resumable, checkpointed recovery |
| [0009](adr-0009-output-safety.md) | Output safety: path confinement and bounded allocation |
| [0010](adr-0010-filename-decoding-safe-output.md) | Filename decoding and cross-platform-safe output |

[ADR-0005](adr-0005-read-only-by-default.md) is the one to read first. It is the
guarantee the whole tool rests on, and the only place that defines what a write path
would have to satisfy to exist at all.
