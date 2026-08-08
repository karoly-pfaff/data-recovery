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
| [0011](adr-0011-two-halves-of-the-read-only-guarantee.md) | The two halves of the read-only guarantee — clarifies 0005; its *Validated* half superseded by 0012 |
| [0012](adr-0012-destination-rule-two-tiers.md) | The destination rule is two tiers over physical identity — supersedes 0011's *Validated* half |
| [0013](adr-0013-unresolvable-identity-is-a-decision.md) | An identity the rule cannot resolve is a question the operator may answer — supersedes 0012 |

[ADR-0005](adr-0005-read-only-by-default.md) is the one to read first — the guarantee the
tool rests on, and where the conditions on any future write path are set.
[ADR-0011](adr-0011-two-halves-of-the-read-only-guarantee.md) says which half of it is a
mechanical fact and which is a check, and
[ADR-0012](adr-0012-destination-rule-two-tiers.md) is the check itself, now that it is one.
