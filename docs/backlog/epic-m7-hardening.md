<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M7 — The record, the surfaces, and the gates that were conventions

**Goal:** make the project's own account of itself true before 1.0 freezes it. The M6
architecture audit found the code sound — the first milestone whose layer-leakage answer
is a clean *no* — and the documents describing it wrong in three places, each of which
[M8](epic-m8-release.md) is about to write a compatibility promise from. Plus the two
gates that pass without inspecting anything, and the convention behind them that the next
gate will not inherit.

Nothing here is new capability. Every story removes a way for the project to be confidently
wrong about itself.

**Milestone:** [M7](../roadmap.md#m7--the-record-the-surfaces-and-the-gates-that-were-conventions)

## Outcome / definition of ready-to-close

- **No Accepted ADR contradicts the shipped code**, and the destination rule's real
  decision has an immutable record of its own rather than living in an edited Consequences
  section.
- **The CLI surface is stated once.** `--help` and the parser cannot disagree, because one
  renders from what the other reads.
- **Every gate measures what it is handed.** No root is passed to a gate that then reports
  green over a language it never looked at.
- **A gate that inspected nothing fails**, by mechanism rather than by a convention each
  script copies — including the one gate that predates the convention.
- Each of the four confirmed audit findings has a story, and the audit's five gate
  proposals are all taken.

## Stories

| Story | Title | Size |
|-------|-------|:----:|
| [story-0701](stories/story-0701-adr-0012-destination-rule.md) | ADR-0012 records the two-tier destination rule, and ADR-0005 becomes immutable again | S |
| [story-0702](stories/story-0702-one-flag-table.md) | The CLI surface is stated once: `--help` renders from the table the parser reads | M |
| [story-0703](stories/story-0703-gates-measure-python.md) | The gates measure the Python in `tools/`, and the 763-line seed generator is split | M |
| [story-0704](stories/story-0704-vacuity-refusal-in-gate-files.md) | A gate that inspected nothing fails: the vacuity refusal moves into `gate_files` | S |
| [story-0705](stories/story-0705-adr-immutability-check.md) | An Accepted ADR cannot be edited: the immutability rule becomes a check | S |
| [story-0706](stories/story-0706-citations-resolve.md) | A `path:line` citation that no longer resolves fails the build | S |
| [story-0707](stories/story-0707-unresolvable-identity-override.md) | A source whose identity cannot be resolved is a decision, not a dead end | M |

Six of these come from the
[M6 architecture audit](epic-m6-loose-ends.md#milestone-architecture-audit), run
2026-08-04 over `v0.3.1..HEAD`; story-0707 came from the first real drive (below).
**story-0701 and story-0702 gate [M8](epic-m8-release.md)'s documentation story.**

**One ordering constraint, found while the stories were written: story-0705 lands after
story-0701.** story-0701 restores ADR-0005's Consequences to the text that was accepted,
which is an edit to an `Accepted` ADR with no new record marking ADR-0005 superseded —
exactly what story-0705's gate refuses. Sequencing them costs nothing; teaching the gate
an exception would cost it its teeth
([story-0705](stories/story-0705-adr-immutability-check.md) records the three options and
why this one). The remaining stories block nothing and can land in any order.

## What each story is

**story-0701 — the record of the read-only guarantee.** ADR-0011 is `Accepted` and false.
It still describes the destination rule as "a lexical path-prefix comparison in
`RecoverySink`" that "does not hold for raw-device sources", and names story-0609 as work
that would make it true — work that landed inside M6 at `4a4221e`. The rule now lives in
`recovery/DestinationRule` as two tiers over `DeviceIdentity`: a spelling tier for every
source, then a physical-identity comparison over storage extents for device sources,
refusing when either identity cannot be resolved. ADR-0011 even instructed its own
successor — "When story-0609 lands, the Validated half becomes a real check and this
record should say so" — and was itself edited afterwards with the stale half three lines
below left untouched.

Worse, that decision was written into ADR-0005's Consequences *in place* (+14/−2), which
[ADR-0001](../architecture/adr/adr-0001-record-architecture-decisions.md) forbids and which
the ADR index added in the same increment restates. So the new rule has no immutable
record and the old one has no trace of being replaced. This story writes ADR-0012 —
both tiers, `DeviceIdentity`/`StorageExtents` as the seam, refuse-on-unresolvable as the
failure mode, and the Storage-Space/mounted-VHD case it does not catch — marks it as
superseding ADR-0011's Validated half, and restores ADR-0005's accepted text.

**story-0702 — one flag table.** The CLI surface is owned in four places and restated in
three more: the flag constants in `RecoveryOptions`, of which only some reach the shared
list while the path flags stay in a separate mapping and each frontend adds its own; then
hand-written help strings in both frontends, four header comments, and `docs/usage.md`.
It has already drifted — `--help` is a real accepted flag that neither usage text
documents, and `--force-portable` is in both help texts but not in `docs/usage.md`.

One descriptor table becomes the source: name, whether it takes a value, and its help
line. The fix pattern is already in the same file — `usage()` renders format names from
the carve layer "so the help can never offer a name the allowlist would then refuse". The
gate is the audit's help-versus-parser proposal, which reduces to comparing two rendered
strings once the table exists.

**story-0703 — the gates measure the Python they are handed.** `tools/` is passed as a
root to the file-length and duplication gates, while their shared file discovery admits
only `.cpp` and `.hpp`. M6 moved 2,115 new lines of Python into that blind spot — 13
files/1,681 lines at `v0.3.1` to 28/3,796 at HEAD — including `tools/fuzz/make_seed_corpus.py`
at 763 lines, three times the hard fail and the largest source file in the tree.

The exclusion is deliberate and unit-tested, so widening it is an
[AGENTS.md](../../AGENTS.md) §2 scope decision, not a bug fix, and the story carries that
amendment. The duplication threshold for Python must be **chosen from a measurement**
rather than converted from the C++ number — the same discipline
[story-0602](stories/story-0602-python-duplication-gate.md) applied when it picked 60
tokens. Splitting the seed generator is the consequence, not the goal.

**story-0704 — the vacuity refusal becomes a mechanism.** Five gate scripts each carry
their own copy of "an empty file set fails"; `check_file_length.py` — which enforces the
§2 headline number — has neither the guard nor a unit test, and is the only script in
`tools/lint/` without one. Six instances of the class in one milestone, plus five bespoke
one-offs answering it elsewhere. The refusal moves into the shared `gate_files`, the five
copies go, and a meta-test discovers every `check_*.py` by glob and asserts each exits
non-zero over a root that exists and holds nothing — so the *seventh* gate inherits the
guard instead of remembering it.

**story-0705 — ADR immutability becomes a check.** ADR-0001 and the ADR index both state
the rule in prose and nothing enforces it; M6 breached it in the same commit range that
documented it. A script over `git diff` fails when the Decision or Consequences section of
an `Accepted` ADR changes, unless the same change adds a new ADR or marks the old one
Superseded. This is the check that would have turned the audit's highest-severity finding
into a red build.

**story-0706 — a citation that no longer resolves.** Stale `path:line` references were
fixed by hand four times inside M6 — the single most repeated review finding of the
increment, and one that consumes adversarial audit rounds, the most expensive resource
this project spends. Six citations are provably past EOF at HEAD and 162 survive across 14
story files. A script resolves every `path:line` and `path:line-line` in `docs/**.md`
against the tree and fails naming the doc, the line and the reason.

**Its limit is stated up front:** it catches a missing path or a range past EOF, not a
citation pointing at the wrong line in a file that is long enough. The gate stops the
class regrowing; what removes it is the rule that goes with it, one sentence in
[code-quality.md](../code-quality.md)'s checklist — **cite code by symbol name, because
only the name survives a rebase.**

## The story the first real drive found

It came out of pointing the shipped `v0.4.0` binaries at a VeraCrypt-unlocked
external disk on 2026-08-04 — the first time this tool met storage nobody had built a
fixture for.

**story-0707 — an unresolvable identity is a decision, not a dead end.** ADR-0005's
destination rule has two tiers, and the second refuses when it cannot resolve *either*
side's physical identity (`refuseOverlap`, `src/recovery/DestinationRule.cpp`). A
VeraCrypt volume has no resolvable identity: Windows maps no partition or disk behind it,
which is the question the rule asks. With such a volume as the **source**, `storageOf`
fails and every destination is refused, so **Revenant cannot be run against a VeraCrypt
volume at all** — not degraded, not warned, refused. (A VeraCrypt *destination* fails on
the `storageUnder` side and costs only that one destination. The first version of this
paragraph named `storageUnder` for both, which inverts what is refused; story-0701's
self-audit caught it.)

The containers the rule cannot see through — listed in
[ADR-0012](../architecture/adr/adr-0012-destination-rule-two-tiers.md) — are the same
blind spot with the opposite sign: cases of the rule being too *permissive*. This one
makes a normal recovery scenario, an encrypted drive, impossible.

The fix is a flag, and its whole design is in one sentence: **it may relax the
unresolvable case and must never touch the proven-overlap case.** If the tool can show
the destination sits on the source, nothing overrides that. If it cannot tell, the
operator may state that they checked, and the run records that it started on an
unverified identity so the manifest carries the fact. Acceptance needs a test that fails
if the override reaches the proven branch — the whole safety value is in that separation.

**The measurement that went with it moved to [M9](epic-m9-acquisition-damaged-media.md).**
Carving a real disk of photographs and video against the live filesystem as ground truth
was scoped here as story-0708, and is now an unnumbered candidate in M9's list. It never
had a story file, so it never held a number ([README.md](README.md#numbering): a number is
allocated when the file is written) — and M9 is where work against real physical media
belongs, next to acquisition, rather than in a milestone whose subject is the project's
own record of itself. Nothing in M7 depends on it; it depended on story-0707, which lands
here, so it is unblocked whenever M9 opens.

## Notes

- **Why this milestone exists at all.** M6's audit confirmed four findings and refuted
  four. The refuted ones matter as much: three were layer-leakage claims, and all three
  fell — [story-0613](stories/story-0613-layer-dag-gate.md)'s gate held, and no layer
  depends upward. The code was not the problem. The documents were, and 1.0 is a promise
  written from documents.
- **The release moved rather than absorbing this.** These seven stories could have been
  appended to the release milestone; keeping them separate means M8 opens with its sources
  already true, and means a milestone whose identity is "ship it" is not also the milestone
  that rewrites the ADRs it ships against.
- **A third ADR-versus-code claim, found while story-0701 was being written** and recorded
  here because it was in neither the audit's findings nor epic-m6's observations.
  [ADR-0007](../architecture/adr/adr-0007-block-level-access-boundary.md) states that the
  CLI "warns about unreliable destination storage". Nothing in the tree does: the only
  warning on that path is `RunSummary`'s, about volume metadata. It is the same shape as
  the two below, it was out of story-0701's scope, and story-0705's gate would not catch it
  because ADR-0007 has never been edited. It needs either a story or a correction before
  [M8](epic-m8-release.md) writes 1.0's documentation from these records.
- **Eight lower-severity observations** from the same audit were passed through
  unverified and are recorded in
  [epic-m6](epic-m6-loose-ends.md#milestone-architecture-audit) rather than queued here.
  Two are worth a second look while this milestone is open, because a story above touches
  their neighbourhood: ADR-0008 names the bad-sector map as durable session state and the
  checkpoint does not persist it, and ADR-0007 justifies network sources by decorators that
  `SourceStack::over` no longer composes. Both are ADR-versus-code claims of exactly the
  kind story-0701 exists to settle, and story-0705's gate would have caught neither, since
  neither ADR was edited.
