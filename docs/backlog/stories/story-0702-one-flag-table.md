<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0702: The CLI surface is stated once — `--help` renders from the table the parser reads

- Epic: [epic-m7-hardening](../epic-m7-hardening.md)
- Status: In progress
- Size: M

## Goal

The set of flags Revenant accepts is written down in seven places and enforced in none of
them. Make one descriptor table the source, render both frontends' help from it, and gate
the two against drifting apart — so that 1.0's man pages are written off something that
cannot lie.

## Design references

- [`src/cli/RecoveryOptions.cpp`](../../../src/cli/RecoveryOptions.cpp) — seven flag
  constants, plus `pathFieldOf`, the separate mapping the three path flags go through.
- [`src/cli/CarveOptions.cpp`](../../../src/cli/CarveOptions.cpp) and
  [`src/cli/UndeleteOptions.cpp`](../../../src/cli/UndeleteOptions.cpp) — one and three
  more flag constants, each frontend's own.
- [`src/cli/Frontend.cpp`](../../../src/cli/Frontend.cpp) — `kHelpFlag`, handled before
  either frontend's parser sees the arguments.
- [`src/cli/CarveCli.cpp`](../../../src/cli/CarveCli.cpp) — `kGrammar`, the hand-written
  help, and `usage()`, **the pattern this story generalises**: format names are listed
  from `carve::builtinFormatNames()` "so the help can never offer a name the allowlist
  would then refuse".
- [`src/cli/UndeleteCli.cpp`](../../../src/cli/UndeleteCli.cpp) — the second hand-written
  grammar.
- [`docs/usage.md`](../../usage.md) — the third restatement, and the one a user reads.
- [epic-m8](../epic-m8-release.md) — story-0802's man-page gate is the second half of
  this pair and cannot be written until this lands.

## What is actually there

Twelve flags, owned in four files:

| File | Flags |
|------|-------|
| `RecoveryOptions.cpp` | `--source` `--destination` `--session` `--dry-run` `--list-partitions` `--partition` `--force-portable` |
| `UndeleteOptions.cpp` | `--hybrid` `--fs-only` `--carve-only` |
| `CarveOptions.cpp` | `--formats` |
| `Frontend.cpp` | `--help` |

and restated in three more: `kGrammar` in each of the two `*Cli.cpp` files, and
[`docs/usage.md`](../../usage.md). It has already drifted, in both directions:

- **`--help` is accepted and documented nowhere as a flag.** `usage.md` mentions
  `revenant-carve --help` only in passing, as the way to list format names.
- **`--force-portable` is in both help texts and absent from `usage.md`.**

Neither is a bug a user hits today. Both are the reason 1.0 must not transcribe these
sources by hand.

## Verified before implementing (2026-08-06)

Two facts the story was written without, both of which change the gate rather than the
table:

**`--help` is not parsed by the grammar at all.** `Frontend::wantsHelp` searches the whole
argument list for it and prints the usage *before* the grammar runs, so it is accepted
anywhere — including beside arguments the grammar would refuse — and `parseCarveOptions` /
`parseUndeleteOptions` never see it. So "the set of flags the parser accepts" literally
excludes `--help`, and a gate asserting set equality against the help text fails on it
immediately. `--help` is therefore a *universal* descriptor that the help renders and the
frontend consumes, and the gate has to say so rather than pretend one mechanism.

**Each frontend's `ExtraFlags` handler doubles as the unknown-flag refusal.**
`applyModeFlag` and `applyFormatsFlag` return `usageError()` for anything they do not own —
they are the last link in `readOne`'s chain, not just "this frontend's flags". Moving the
flags into a table therefore moves that refusal too: the table lookup becomes the thing
that fails an unknown flag. This is why the refactor cannot be additive, and it is the part
most likely to change behaviour by accident, so the existing parser tests passing untouched
is the acceptance criterion that matters.

## Design decisions

**One descriptor, three fields, and the parser reads it.** `name`, whether it takes a
value, and its help line. The table is the *parser's* input — not a parallel structure the
help renders from and the parser ignores, which would drift exactly as today's does, only
with more ceremony. If a flag is not in the table, the parser does not accept it.

**Rendering follows `usage()`'s existing shape.** `CarveCli.cpp` already proves the
pattern in this very file: the grammar line is a constant, and the varying part is
rendered from the layer that owns it. This story moves the flag list across the same seam.
The grammar's *shape* — which flags are positional-ish, which are alternatives, the
`--list-partitions` second form — stays hand-written, because that is genuinely prose
about how the flags combine and no table encodes it. **What is generated is the flag list
and its help lines; what stays written is the synopsis.** Conflating the two would be a
worse abstraction than the duplication it removes.

**Three flag scopes, not one flat list.** `--help` is universal, seven flags are shared by
both recovery frontends, and four are one frontend's own. A single global table would make
`revenant-carve --hybrid` renderable, which is the mistake in the opposite direction.
The table is composed per frontend: universal + shared + own.

**`pathFieldOf` goes.** The three path flags reach `OptionDraft` through a second mapping
that exists only because the flag constants are not data. Once they are, a descriptor
names its own destination field and the special case disappears. This is the concrete
simplification that tells us the table is real rather than decorative — if `pathFieldOf`
survives, the refactor did not land.

**The gate compares two rendered strings.** The audit's help-versus-parser proposal
reduces, once the table exists, to: for each frontend, the set of flags the parser accepts
equals the set the help text names. That is a unit test over two functions in the same
translation unit — no subprocess, no golden file to rot. A golden `--help` snapshot was
considered and rejected: it fails on every wording change, which trains reviewers to
re-bless it, and re-blessing is how the current drift got in.

**`docs/usage.md` is brought into line by hand, and is not generated.** Generating prose
documentation from the table is a bigger change than this story, and a usage guide that is
only a flag list is worse than the one there now. What this story owes `usage.md` is
accuracy: `--help` and `--force-portable` documented, and nothing listed that the parser
does not accept. story-0802 owns the man pages and the gate that binds them.

## Acceptance criteria

- [ ] One descriptor type carries a flag's name, whether it takes a value, and its help
      line; the parser dispatches from it.
- [ ] Each frontend composes its table from universal + shared + own flags; no frontend
      can render or accept another's flag.
- [ ] `--help` for both binaries lists every flag that binary's parser accepts, with no
      flag listed that it does not accept — plus `--help` itself, which the frontend
      consumes before the grammar runs and which is declared universal for that reason.
- [ ] A unit test asserts that correspondence per frontend, and fails when a flag is added
      to the parser without a help line.
- [ ] An unknown flag is still a usage error after the `ExtraFlags` handlers stop being the
      last link in the chain.
- [ ] `pathFieldOf` no longer exists; path flags reach `OptionDraft` through the
      descriptor.
- [ ] `docs/usage.md` documents `--help` and `--force-portable`, and lists no flag the
      parser rejects.
- [ ] The hand-written `kGrammar` synopsis remains, and no longer enumerates flags that
      the table also enumerates.
- [ ] Exit-code help (`kExitCodes`) is unchanged — out of scope, and stated so.

## Test plan

Unit (`tests/unit/cli/`):

- `every_accepted_flag_appears_in_help` — per frontend, the descriptor set and the
  rendered help agree. **Must fail** when a flag is added to the parser with no help
  line; that negative is the test's whole point and is demonstrated, not assumed.
- `no_frontend_renders_another_frontends_flag` — `revenant-carve`'s help does not name
  `--hybrid`; `revenant-undelete`'s does not name `--formats`.
- `a_flag_taking_a_value_reports_a_usage_error_when_it_is_last` — the value-taking bit in
  the descriptor is load-bearing, so it is tested through behaviour rather than read back.
- `parsing_is_unchanged` — the existing `RecoveryOptions` / `CarveOptions` /
  `UndeleteOptions` parser tests pass untouched. This is a refactor; if they needed
  editing, the surface moved and that is a finding, not a fixup.

Integration: the two binaries' `--help` exits 0 and its first line is the synopsis —
enough to catch a rendering crash, and deliberately not a golden-text comparison.

Not automated: that the help *wording* is good. A gate can hold the set of flags; only
review holds whether a help line explains anything.

## Definition of Done

- [ ] Acceptance criteria met, tests green (ASan + UBSan).
- [ ] Coverage held or raised (≥ 85% core).
- [ ] clang-format, clang-tidy, duplication, file-length guard clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
- [ ] `docs/usage.md` updated; [epic-m8](../epic-m8-release.md)'s story-0802 note still
      accurate about which half of the pair exists.
