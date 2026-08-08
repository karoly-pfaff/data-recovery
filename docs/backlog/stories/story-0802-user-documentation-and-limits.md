<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0802: Documentation a frightened stranger can follow, and an honest page of limits

- Epic: [epic-m8-release.md](../epic-m8-release.md)
- Status: Ready
- Size: M

## Goal

Man pages, a usage guide, a recovery playbook in the order someone with a dead SD
card should actually work, and `docs/limitations.md` — an honest page on what
Revenant cannot do. Plus the gate that keeps them from rotting: a test that fails
when a flag the parser accepts is missing from the man page.

## Design references

- [ADR-0013](../../architecture/adr/adr-0013-unresolvable-identity-is-a-decision.md)
  — the destination rule as it stands, including the operator override. **Not
  ADR-0012**, which it supersedes; see the precondition below.
- [ADR-0011](../../architecture/adr/adr-0011-two-halves-of-the-read-only-guarantee.md)
  — which half of the read-only guarantee is structural and which is a check.
  The limits page must not promise the second as if it were the first.
- [versioning.md](../../versioning.md) — from 1.0 the CLI flags, the on-disk
  output layout and recovery accuracy per supported format are the surface. This
  story is where that surface gets written down, which is why it precedes the tag.
- `src/cli/FlagTable.cpp` (story-0702) — the one table the parser and `--help`
  both read. `revenant::cli::kExitCodes` in `src/cli/ExitCodeHelp.hpp` — the five
  exit codes, in one place.

## Precondition, not a tidy-up

The M7 architecture audit found **ADR-0012 still reading `Status: Accepted`**
after ADR-0013 named it superseded, and
[epic-m8](../epic-m8-release.md) pointing this story at ADR-0012 as "the
authority for the rule". Written from that record, 1.0's limits page would
describe a destination rule with no operator override and omit the class of run
whose safety rests on an operator's assertion. **ADR-0012 is demoted before this
story starts.** Gate 14 permits the demotion — it is a Status line, outside the
frozen sections — but does not require it, which is why it needs saying here.

## Measured before writing (2026-08-08)

- **No man pages exist**, and no `man/` directory. `docs/usage.md` is 102 lines
  over six sections; `docs/install.md` is 218 and documents building from source
  only. `docs/limitations.md` does not exist.
- The flag surface is one table (`src/cli/FlagTable.cpp`), and `--help` renders
  from it — story-0702's half of the pair. **This story supplies the other half**:
  nothing today can see a flag the parser accepts that the documentation omits.
- Exit codes are one `string_view` in `ExitCodeHelp.hpp`: five codes, `0`–`4`.

## What the limits page must say, and where each limit already lives

Written down rather than rediscovered — every one of these is already recorded
somewhere in the repository, and the page's job is to gather them, not to invent
them.

- **Fragmented files.** Carving reconstructs contiguous runs; a fragmented file
  comes back partial or not at all.
- **Encrypted volumes.** The source's physical identity may be unresolvable, in
  which case the destination check cannot answer and refuses unless the operator
  overrides it — [ADR-0013](../../architecture/adr/adr-0013-unresolvable-identity-is-a-decision.md).
- **TRIMmed SSDs.** Deleted data is usually gone at the device, not merely
  unreferenced. No tool recovers it, and saying so is not an apology.
- **Containers the destination check cannot see through** — the list lives in the
  destination-rule ADR and nowhere else, and the page cites it rather than
  copying it.
- **What deletion destroys differs by filesystem.** An NTFS undelete can return
  the file exactly; a FAT one is a graded guess, and the manifest's confidence
  field is where that shows.
- **Acquiring a failing drive is M9's**, which is why the playbook's first step
  points at `ddrescue` and not at Revenant.
- **Two limits M6 recorded**: the fuzz campaign ran 14.5 CPU-hours against the
  112 first scoped, so bugs behind a multi-stage input are unexplored; and
  CodeQL's C++ library declares no flow-source model for this tree's read
  primitives (story-0615), so static taint analysis covers the parsers'
  arithmetic and not the device boundary.

**ADR-0007 is not a source for this page.** It states a warning the code does not
emit and two other contradictions, recorded in
[epic-m7's residue](../epic-m7-hardening.md#residue-findings-with-no-story) and
owned by no story. Gate 14 catches an *edit* to an Accepted ADR; it cannot catch
an *inaccuracy*. Transcribing it into user-facing documentation is how an
inaccuracy becomes a promise.

## Design decisions

**The gate compares the man page against the flag table, not against `--help`
output.** Both render from the same table, so parsing `--help` would add a
process launch and a text format to go wrong in between. The gate reads
`FlagTable.cpp`'s entries and `kExitCodes`, and fails when either names something
the man page does not. It must refuse to pass vacuously — zero flags parsed is a
failure, not a clean run, in the shape story-0704 established.

**The playbook is ordered by what a frightened person should do, not by what the
tool offers.** Stop writing to the card; image it; work on the copy;
`--list-partitions`; `--dry-run`; run; verify the manifest. The first two steps
are not Revenant's, and the page says so in the first two sentences.

**The limits page is not an apology.** A recovery tool that overstates itself
costs somebody their photographs; one that hedges everything gets closed. Each
limit states what happens, why, and what the user can do instead.

**Man pages are hand-written**, one per binary, and are the packaged artefact
story-0801 installs to `share/man/man1/`. No generator: a generated man page is
one more thing whose output nobody reads.

**`docs/install.md` gains the package path** — story-0801's deliverable — rather
than a second document about installing.

## Acceptance criteria

- [ ] `man/revenant-undelete.1` and `man/revenant-carve.1` exist, render under
      `man`, and document every flag and every exit code.
- [ ] `docs/limitations.md` exists and covers each limit listed above, each
      citing where the authority for it lives.
- [ ] The recovery playbook exists and its first step is imaging, not recovery.
- [ ] A flag added to `FlagTable.cpp` and to nothing else makes the new gate
      fail, and the failure names the flag.
- [ ] An exit code added to `kExitCodes` and to nothing else makes it fail.
- [ ] The gate refuses to pass when it parsed no flags.
- [ ] No user-facing page cites ADR-0012 or ADR-0007 as an authority.
- [ ] The citation gate (gate 15) is green over the new documents.

## Test plan

- **Unit, `tests/unit/lint/`** — the new gate against synthetic inputs: a flag in
  the table and absent from the man page fails and names it; an exit code absent
  fails; a man page documenting a flag that no longer exists fails; a table the
  gate could not parse fails rather than passing empty. The vacuity case is
  asserted by mechanism, as `test_gate_vacuity.py` does.
- **Against this repository** — the gate over the real man pages and the real
  table, green. This is the case that would have caught the drift the epic
  describes, and it is worthless if it only ever runs on fixtures.
- **Watched failing** — for each of the four unit cases, the assertion is watched
  failing with the gate's corresponding check reverted. A gate that reports a
  clean man page it never read is the failure mode this milestone's own audit
  found twice; naming the test that breaks is the only thing that rules it out.
- **No C++ test.** Nothing here changes behaviour; a test asserting a document's
  contents in C++ would be a second copy of the gate.

## Definition of Done

- [ ] Acceptance criteria met, tests green (ASan+UBSan).
- [ ] Coverage held or raised (>= 85% core).
- [ ] clang-format, clang-tidy, duplication, file-length guard clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md))
      completed.
- [ ] The new gate is in `cmake/DevTargets.cmake` **and** `ci.yml` **and**
      `quality-gates.md` **and** `gate-runner`'s list — the four places M7's
      audit found two gates missing from.
