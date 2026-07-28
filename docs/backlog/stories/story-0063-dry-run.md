<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0063: `--dry-run` — the winners without the writing

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: S

## Goal

Let an operator see what a recovery would produce before committing disk space
and time to it. Scanning a terabyte, watching it fill a destination with the
wrong things, and starting again is the expensive mistake this prevents.

The story is small for one reason: [ADR-0006](../../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md)
already separated deciding from writing, so a preview is not a mode the engine
has to learn — it is simply *stopping before the last step*.

## Design references

- [ADR-0006](../../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md) —
  discover → arbitrate → extract. A preview is the first two.
- [Recovery output](../../architecture/recovery-output.md) — "`--dry-run`
  performs the full scan, validation, and arbitration but **writes no
  artifacts**. It emits the manifest of what *would* be recovered."
- [ADR-0008](../../architecture/adr/adr-0008-resumability-checkpointing.md) — the
  preview still fills the session directory, which is what lets story-0064 promote
  one into a real extraction without re-scanning.

## Scope

1. **`RecoverySink::preview`** — every winner as it *would* be written: the same
   names, the same order, the same collision renames, and nothing created.
2. **`ArtifactOutcome::kPreviewed`** — so the manifest says plainly that these
   artifacts were never written.
3. **`--dry-run`** on both frontends, and a summary line that reads as a preview
   rather than as an extraction that wrote nothing.

## Design decisions

**A preview claims real names.** It runs the same naming path extraction does —
`outputNameFor`, `sanitizeOutputPath`, `disambiguate`, in the same order — so the
paths it reports are the paths a real run would use, renames and all. A preview
that guessed differently from the run it previews would be worse than no preview.

**A preview does not hash.** A digest costs a full read of every artifact, which
is most of what extraction is; a preview that read everything would not be the
cheap look-before-you-leap this exists to be. Previewed artifacts therefore carry
no `sha256` and no size — stating either would mean pretending to know something
this run did not look at.

**`filesWritten` stays zero, because nothing was written.** The counts a preview
reports are the ones it can honestly fill: the renames it performed while naming,
and the winners whose name has no safe form and would therefore fail. An
estimated output size belongs with the free-space check that will use it, not
here.

**A preview still fills the session directory.** The candidate index and the
manifest are what a preview is *for* — and ADR-0008 wants a preview to be
promotable into a real extraction without re-scanning, which it cannot be if it
leaves nothing behind. Only the destination stays untouched.

**The destination is still validated.** "Your destination is a file" is exactly
the kind of thing a preview should tell you, so `--dry-run` refuses the same
destinations a real run does.

**`--dry-run` is a shared flag, not a per-frontend one.** Both binaries run the
same three steps, so both stop before the same one. It lives in the shared
grammar next to `--source`, and a repeated `--dry-run` is refused for the same
reason a repeated mode flag is.

## Acceptance criteria

### `RecoverySink::preview`

- [x] Returns one artifact per winner, each with the name it would be written
      under, and outcome `kPreviewed`.
- [x] Creates nothing: no files, no directories, in the destination.
- [x] Names collide and disambiguate exactly as extraction would, and the rename
      is counted.
- [x] A winner whose name has no safe form is counted as failed and carries no
      written name.
- [x] `filesWritten`, `bytesWritten` and `deduplicated` stay zero, and no
      artifact carries a hash.
- [x] Named artifacts precede carved ones and carved ordinals match extraction's,
      so the preview's names are the run's names.

### `--dry-run`

- [x] Both frontends accept it; a second one is `kInvalidArgument`.
- [x] The run scans, indexes, arbitrates and writes the manifest, and the
      destination holds nothing but the session directory.
- [x] The manifest records the artifacts as `previewed`.
- [x] A destination that is unusable is still refused.
- [x] The summary says `preview` and reports what a preview can know.

## Test plan

Unit (`tests/unit/recovery/RecoverySinkTest.cpp`): a preview of resident and
extent-backed winners names them all and writes nothing; two winners wanting one
name are disambiguated and counted; an escaping name is counted as failed; carved
ordinals match what extraction produces for the same winner list.

Unit (`tests/unit/cli/UndeleteOptionsTest.cpp`, `CarveOptionsTest.cpp`):
`--dry-run` sets the preview delivery; the default is extraction; a repeated flag
is refused.

Unit (`tests/unit/cli/RunSummaryTest.cpp`): a preview report reads as a preview.

Integration (`tests/integration/UndeleteCliTest.cpp`): `--dry-run` over the
fixture image leaves the destination holding only the session directory, and a
manifest naming `photos/deleted.jpg` as `previewed`.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Coverage held or raised (>= 85% core) — enforced by the CI coverage job;
      the instrumentation is GCC/Clang-only, so it is not reproducible on the
      local MSVC build.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `README.md` updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
