<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0017: `revenant-undelete` CLI — modes, source, destination

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: M

## Goal

Give the finished engine a front door. Every layer of the vertical slice works and
is proven end to end by
[`RecoveredFilesTest`](../../../tests/integration/RecoveredFilesTest.cpp) — but the
only thing that can run it is a test. This story ships the first real binary:
`revenant-undelete`, which mounts a source, recovers it in the mode the operator
asked for, and writes the winners to a destination.

## Design references

- [Hybrid orchestration](../../architecture/hybrid-orchestration.md) — the three
  modes (`--fs-only`, `--hybrid`, `--carve-only`) and, decisively, the closing
  sentence: *"The CLI merely maps flags onto this surface; the policy lives in
  `recovery/`."*
- [ADR-0006](../../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md) —
  the run's shape is fixed by the architecture: discover → arbitrate → extract.
  The CLI sequences those three; it does not decide between candidates.
- [ADR-0005](../../architecture/adr/adr-0005-read-only-by-default.md) — the source
  is opened read-only and the destination is somewhere else; `RecoverySink`
  enforces it and the CLI asks it to, early.
- [Recovery output](../../architecture/recovery-output.md) — the session directory
  is where a run's durable state lives; the manifest that will join it there is
  [story-0062](../epic-m1-vertical-slice.md).

## Scope

1. **The grammar** — `parseUndeleteOptions`, turning the argument list into an
   `UndeleteOptions` value:

   ```
   revenant-undelete --source <image> --destination <directory>
                     [--hybrid | --fs-only | --carve-only]
                     [--session <directory>]
   ```

2. **The run** — `runRecovery(RunRequest)`: open the device, validate the
   destination, create the session index, run `HybridRecovery`, arbitrate, and
   extract the winners. Every step is a call into `recovery/`; none of them is a
   decision made here.
3. **The words** — `summarize(RunReport)` and `describe(Error)`, so a run can say
   what it found, what it chose, what it wrote, and why it stopped.
4. **The frontend** — `runUndeleteCli(argv)` (usage, logging, exit status) plus a
   three-line `main`.
5. **CMake** — a `revenant_cli` static library holding all of the above, and the
   `revenant-undelete` executable over it.

## Design decisions

**The CLI holds no policy.** Every flag names a value the recovery layer already
defines — `RecoveryMode`, a source path, a destination path. There is no
threshold, no ordering rule, and no naming scheme here; those live in `recovery/`
and this story deliberately adds none. That is what the architecture document
asks for, and it is also what keeps the binary swappable: a GUI over the same
engine would make the same calls.

**Logic in a library, `main` in three lines.** The frontend is a static library
(`revenant_cli`) that the test binary links, exactly as `revenant-imagegen`
already does. A CLI that can only be tested by spawning a process is a CLI that
does not get tested.

**The destination is validated before the scan, not after it.** `RecoverySink::open`
only *checks* — it creates nothing — so the run can call it first and refuse a
missing destination, a file-as-destination, or a destination containing the source
in the first second rather than after an hour of scanning. Extraction still happens
last (ADR-0006); only the validation moves forward.

**The session directory defaults to `<destination>/.revenant`.** A run needs
somewhere durable to put its candidate index, and the destination is the one
directory already known to exist, to be writable by intent, and not to be the
source. `--session` overrides it for an operator who wants run state elsewhere —
and story-0064, which turns that state into something a second run resumes from,
is why the flag is named now rather than left implicit. A volume whose own
top-level tree contains a `.revenant` directory would have those entries
reconstructed into the session directory; that is what `--session` is for, and a
collision-proof default belongs with the story that makes sessions long-lived.

**A lost index record ends the run.** `IndexingEntryVisitor` and
`IndexingCandidateVisitor` count failed appends because a visitor has no way to
return one, and they offer the count precisely so someone acts on it. The CLI is
the first place that can. An index that quietly lost a record makes the winner
set, the suppression count, and the output all wrong — so the run fails with
`kIoFailure` instead of reporting a smaller world confidently.

**The summary reports what an operator can act on.** Three lines: what was found,
what arbitration chose, what was written. `RecoveryStats::regionsDropped` is
deliberately not among them — ADR-0009 makes dropping an accounting region mean
*more* scanning, never less recovery, so it is a performance fact rather than a
recovery fact, and the full per-run record is story-0062's manifest.

**A second mode flag is a contradiction, not a refinement.** `--fs-only
--carve-only` is a usage error rather than "last one wins". Guessing which of two
contradictory instructions an operator meant is exactly the silent-wrong-thing the
contract forbids.

**No fuzz target here.** The contract requires one for every parser that reads
external *bytes*, because parsing hostile device content is the threat model.
`parseUndeleteOptions` compares whole `string_view`s against a fixed flag list and
copies the rest into paths: no offsets, no lengths, no allocation sized by input.
The bytes this story does put through a parser — the source image — reach carvers
and filesystem parsers that are already fuzzed.

## Acceptance criteria

### `parseUndeleteOptions`

- [x] `--source` and `--destination` are required; either missing is
      `kInvalidArgument`.
- [x] The mode defaults to `kHybrid`, and `--hybrid` / `--fs-only` /
      `--carve-only` select it.
- [x] A second mode flag is `kInvalidArgument`, even when it repeats the first.
- [x] An unknown flag is `kInvalidArgument`.
- [x] A value flag with no value after it is `kInvalidArgument`.
- [x] `--session` sets the session directory; without it, the session directory
      is `<destination>/.revenant`.

### `runRecovery`

- [x] A source that cannot be opened fails before anything is created.
- [x] A destination that does not exist, is not a directory, or contains the
      source fails before the scan, not after it.
- [x] The session directory is created if it is not already there.
- [x] The run appends both sources' findings to one index, arbitrates it, and
      extracts only the winners.
- [x] A failed index append fails the run (`kIoFailure`).
- [x] The report carries the discovery stats, the winner and suppressed counts,
      and the extraction stats.

### `revenant-undelete`

- [x] `--help` prints the usage and succeeds.
- [x] No arguments, or unparseable ones, print the usage to stderr and fail.
- [x] A hybrid run over the fixture image recovers the named files at their paths
      and the unreferenced JPEG under `carved/`.
- [x] `--fs-only` recovers the named files and carves nothing.
- [x] `--carve-only` carves without reconstructing any name.
- [x] The process exit status is 0 on success and 1 on any failure.

## Test plan

Unit (`tests/unit/cli/UndeleteOptionsTest.cpp`): a minimal valid command line; each
mode flag; the default mode; a repeated and a contradictory mode flag; a missing
source; a missing destination; an unknown flag; a trailing flag with no value; an
explicit `--session`; the derived default session path.

Unit (`tests/unit/cli/RunSummaryTest.cpp`): the three summary lines carry their
counts; an unmounted filesystem is stated rather than implied; every `ErrorCode`
describes as a non-empty sentence.

Integration (`tests/integration/UndeleteCliTest.cpp`): the CLI driven over the
story-0065 fixture image, once per mode. Hybrid brings back `photos/deleted.jpg`
byte-for-byte *and* the carved JPEG; `--fs-only` brings back the named files and
creates no `carved/` bucket; `--carve-only` creates `carved/` and no `photos/`
tree. Plus the failure surface: no arguments, an unknown flag, a missing source, a
destination that is a file, and `--help`.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Coverage held or raised (>= 85% core) — enforced by the CI coverage job;
      the instrumentation is GCC/Clang-only, so it is not reproducible on the
      local MSVC build.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `README.md` usage updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
