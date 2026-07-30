<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0614: One job per document, and the read-only guarantee checked by a test

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: In review
- Size: M

## Goal

The documentation that forbids duplicated knowledge is built out of it. Fourteen facts
are stated in two to eleven places each; six have already drifted apart, and two of those
disagree about what the merge gates are and whether we push. Give every document one job,
name one owner per fact, and make the rest reference it. And close the gap the survey
found underneath: the strongest promise this project makes — the source device is never
written — is enforced by construction but asserted by nothing. A regression that opened
the source read-write would pass the entire suite.

## Design references

- [code-quality.md](../../code-quality.md) — "DRY is about *knowledge*, not textual
  similarity", and the arbitration rule at the top of the file: where `AGENTS.md` and
  this file appear to differ, AGENTS.md wins and code-quality.md is the bug. Finding A1
  below is that rule pointed at a gate which does not exist.
- [ADR-0005](../../architecture/adr/adr-0005-read-only-by-default.md) — the read-only
  authority, and the only place the escape hatch is defined.
- [story-0602](story-0602-python-duplication-gate.md) — the precedent for what a gate
  finds on day one: fixed or justified, never tuned away. Applied here to prose.
- [quality-gates.md](../../testing/quality-gates.md) §"Changing a gate" — a gate change
  is a dedicated PR updating AGENTS.md, that file, and the tool config together. This
  story changes no gate; it makes the documents agree about the gates that exist.
- [story-0609](story-0609-destination-on-source-refused.md) — cites `adr-0005:30-31`,
  `recovery-output.md:63-64` and `RecoverySink.hpp:60-62` by line. This story must not
  erase the discrepancy that story was written against.

## What was measured

Surveyed 2026-07-30 across the root documents and `docs/` (~2 500 lines, 30 files).

**Six facts have already drifted**, in order of damage:

| # | Fact | The drift |
|---|------|-----------|
| A1 | The merge-gate list | `AGENTS.md` and `code-quality.md` both require **`cppcheck`**. It appears nowhere else — not in `ci.yml`, not in a CMake target, not in a story. The authoritative 9-gate table in `quality-gates.md` has no such row. |
| A2 | Running `tidy` on Windows | Three incompatible answers: `CLAUDE.md` prints the command with no caveat (and it fails on this machine), `quality-gates.md` says use the `release` preset, `install.md` says hand-configure a sanitizer-free build dir. The release-preset form is what every story transcript actually used. |
| A3 | The push policy | `AGENTS.md` says pushes happen **only at minor releases**; `git-workflow.md`, `contributing.md` and `quality-gates.md` describe per-story PRs with green CI on every push. Both cannot hold. `AGENTS.md` cites `settings.md` as the authority — **that file does not exist**. |
| A4 | Test-first | Unconditional in five documents; `code-quality.md`'s self-audit checkbox says "where practical" — and that checkbox is the only one of the six anybody ticks per story. |
| A8 | `-Wall -Wextra -Werror` | `AGENTS.md` and `CLAUDE.md` attribute GCC syntax to MSVC. Only `quality-gates.md` names `/W4 /WX`. |
| A9 | The complexity limit | `AGENTS.md` says **cyclomatic**; the enforcing check is `readability-function-cognitive-complexity`. Different measures, different values. The contract names a limit nothing enforces. |

**Structure.** `README.md` is 45% CLI manual (69 of 153 lines) and about eight lines of
map. `CLAUDE.md` is roughly 60 of 89 lines restatement. The entry points form a cycle:
`README` → `CLAUDE.md` for build commands → `AGENTS.md` → back to `CLAUDE.md` for build
commands, while `install.md` says in its own first lines that it owns them. `CHANGELOG.md`
is reachable from no document. There is no root `CONTRIBUTING.md`, and GitHub's detection
is case-sensitive, so `docs/contributing.md` earns none of the affordances `SECURITY.md`
does. Ten relative links are broken.

**The read-only guarantee.** Twenty-six statements across docs, ADRs and code comments,
in four different strengths — "by default", "always", "by construction", "forever". The
three highest-traffic entry points (`README`, `AGENTS.md`, `CLAUDE.md`) all say "by
default" and **none links to ADR-0005**, the only place that defines what the non-default
is. Mechanically: the handle-level guarantee is real and structurally hard to break —
`BlockDevice` declares no write operation, and every open funnels through one
`openReadOnly` with `GENERIC_READ` / `O_RDONLY`. But **no test asserts it**. No
integration test compares the source image before and after a run; no lint scans for
`GENERIC_WRITE`, `O_WRONLY` or `O_RDWR`. The strongest claim in the project rests on
review alone.

## Design decisions

**One job per document, and the owner is named in the document.** Each fact gets exactly
one home; everywhere else references it. The owners, from the survey: gates and coverage
floor → `quality-gates.md`; hard-limit numbers → `AGENTS.md` §2; read-only → ADR-0005;
build commands and toolchain versions → `install.md`; TDD and the fuzz mandate →
`testing/strategy.md`; branch, PR, push and squash policy → `git-workflow.md`; story
numbering → `backlog/README.md`; commits, SemVer, CHANGELOG and the release procedure →
`versioning.md`; streaming → `performance/strategy.md`; milestone status → `roadmap.md`.

**A contract may restate; an index may not.** `AGENTS.md` stays self-contained where a
reader must not have to follow a link to learn a rule — the naming table, the limits, the
non-negotiables. What it may not do is *contradict* the owner or invent a gate. The test:
if the number or the tool name appears in two files, one of them must be the owner and
the other must link.

**`README.md` becomes the map it claims to be.** The CLI manual moves whole to a new
`docs/usage.md` — no content lost, and M7's user-documentation story inherits a real file
instead of a section. README keeps what the project is, the principles, a three-command
quickstart pointing at `install.md`, and an index that reaches everything, including
`CHANGELOG.md` and an ADR index that does not exist yet.

**`CLAUDE.md` keeps only what is agent-specific.** The `.claude/` inventory, the
skill-driven lifecycle, the `fuzz-campaign` / `wsl-bench` routing, the tool-boundary
hooks, and the instruction to read `AGENTS.md` first. Everything else is deleted, not
moved, because its destination already has it. The cycle breaks: nothing points at
`CLAUDE.md` for build commands.

**`docs/contributing.md` moves to root `CONTRIBUTING.md`** so GitHub finds it, and becomes
the human sibling of `CLAUDE.md`: reading order, the licensing statement, the PR-and-ADR
expectation, and links to the owner of every mechanic. Its uniquely-owned content — the
GPL-3.0-or-later contribution statement — survives verbatim.

**The read-only guarantee is unified in the strong direction, and never by deletion.**
Where copies differ in strength, the strongest wording wins and the weaker is raised to
it — "never", not "by default", except where the escape hatch is named in the same
breath. Every restatement gains a link to ADR-0005. ADR-0005 itself stops bundling two
claims of unequal strength under one heading: the handle guarantee is *by construction*,
the destination separation is *validated* — and story-0609 exists because the second is
not yet true for raw devices. The ADR says which is which. The sixteen sentences the
survey listed as load-bearing are preserved verbatim or strengthened; none is softened,
merged away, or moved without a redirect.

**The strongest claim gets a test.** An integration test hashes the source image before
the run and after it and asserts the digest is unchanged. `Sha256` and a whole-file reader
already exist in the tree, so this is small — and it converts "the source is never
written" from a promise into something a merge can fail on. It belongs in this story
rather than a separate one because a docs story whose whole subject is that guarantee
should leave it checkable.

**Not in scope.** No gate is added, changed or removed — `quality-gates.md`'s own rule
makes that a dedicated PR. The lint that would scan for write-mode flags is named as a
candidate and not built. story-0609's fix is not attempted here.

## Acceptance criteria

- [x] Every fact in the survey table has exactly one owning document; every other mention
      links to the owner rather than restating the number, the tool name or the rule.
- [x] `cppcheck` no longer appears as a required gate anywhere, or it exists — the
      documents and the CI configuration agree on the gate list.
- [x] The push policy is stated once, in `git-workflow.md`, and is consistent with
      per-story PRs; no document cites `settings.md`.
- [x] `AGENTS.md` names the complexity measure the tooling actually enforces, and states
      MSVC's warning flags correctly.
- [x] "Where practical" is gone from the test-first checkbox, or `testing/strategy.md`
      states the exception it refers to.
- [x] The Windows `tidy` recipe appears once, and it is the one that works on this
      machine.
- [x] `README.md` contains no CLI reference; `docs/usage.md` contains all of it, with
      nothing lost. README's index reaches every document including `CHANGELOG.md`.
- [x] `CONTRIBUTING.md` is at the repository root; the GPL-3.0-or-later contribution
      statement survives verbatim.
- [x] `CLAUDE.md` states nothing that `AGENTS.md` or a specialised document owns; no
      document points at `CLAUDE.md` for build commands.
- [x] Every relative link in the repository's markdown resolves, and no document
      references a file that does not exist.
- [x] Every read-only sentence the survey listed is present verbatim or in stronger form;
      `README.md`, `AGENTS.md` and `CLAUDE.md` each link to ADR-0005 where they state it.
- [x] An integration test fails if a run modifies its source image.
- [x] The freeze on `AGENTS.md` and `CLAUDE.md` is restored in the same pull request that
      lifts it, and the restoration is verified.

## Test plan

- Integration (`tests/integration/`): a full recovery over a generated image asserts the
  source file's SHA-256 is identical before and after, and its size is unchanged. A
  mutation must fail it, and the mutation has to be one that actually writes: opening the
  source read-write changes nothing observable on its own. Recorded here on completion.
- A link check over every markdown file in the repository, run and recorded; it must
  report zero unresolved relative links. Whether it becomes a gate is story-0612's
  question, not this one's.
- Not automated: that each document has one job. That is what the self-audit is for. The
  mechanical part — one owner per fact — is verified by grep for the numbers and tool
  names the survey listed, and the greps are recorded in this story.

## Verified on completion (2026-07-30)

**One owner per fact.** Greps over the whole tree, story files and the CHANGELOG
narrative excluded: `cppcheck` as a required gate — 0 files. `settings.md` references —
0. "where practical" — 0. `revenant:add-format-carver` — 0. The hard-limit numbers
outside `AGENTS.md` §2 — 0.

The first pass recorded that last figure as zero while grepping only `docs/testing/`,
and the self-audit found three sites it could not see (`git-workflow.md`,
`code-quality.md`, and the carve skill). The number is now measured over everything, and
those sites name the limit instead of quoting it.

**Links.** A check over every markdown file in the repository, resolving both the file
and the heading anchor: **0 unresolved**. The first pass checked files only and missed a
bad anchor in `quality-gates.md`; the self-audit caught it, and the recorded check now
covers anchors.

**The read-only test, proved by mutation.** Writing one byte into the source image
mid-run fails `SourceUnchanged.AFullRecoveryLeavesTheSourceByteForByteIdentical` on the
digest comparison; the source was restored and the suite re-run green afterwards. That
establishes the assertion is wired and would catch a write. It does not exercise the
handle flags — relaxing those alone writes nothing, so nothing would fail; the flags are
guarded by `BlockDevice` having no write operation at all, which is a structural fact
rather than a testable one.

**The freeze.** `.claude/settings.json` is byte-identical to `main`. It was lifted twice
during the work and restored both times; four independent checks confirmed it, the last
after the final edit. The commit touching the two frozen files used `--no-verify`, the
documented maintainer override, with `format-check` and `guard-limits` run by hand first
so nothing the hook checks was skipped.

**Gates.** 1010/1010 under ASan + UBSan; clang-tidy over 561 translation units from
cleared stamps; `jscpd` reports no clones among the three touched test files.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Coverage held or raised (≥ 85% core).
- [x] clang-format, clang-tidy, duplication and file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
- [x] Docs/ADRs updated if the design changed.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
