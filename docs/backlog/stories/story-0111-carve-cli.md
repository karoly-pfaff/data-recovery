<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0111: `revenant-carve` CLI — format allowlist, destination

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: S

## Goal

Ship the second binary — the PhotoRec-shaped one. `revenant-carve` recovers from
a volume nothing can mount: no filesystem, no names, just structure. It is also
the story where the frontend layer stops being one binary's private plumbing and
becomes a layer: everything `revenant-undelete` invented in
[story-0110](story-0110-undelete-cli.md) that is not specific to it moves into
shared code, because the alternative is two copies of the same run.

## Design references

- [Hybrid orchestration](../../architecture/hybrid-orchestration.md) —
  "`revenant-carve` is always carve-only", and the allowlist is part of the
  recovery layer's configuration surface, not the CLI's.
- [story-0206](story-0206-plausibility-filter.md) — the allowlist is applied at
  *registration*, so a format the operator did not ask for costs nothing at all:
  no signature search, no carve attempt.
- [Recovery output](../../architecture/recovery-output.md) — carved artifacts go
  to `carved/<ext>/`, numbered in device order.

## Scope

1. **The grammar** — `parseCarveOptions`:

   ```
   revenant-carve --source <image> --destination <directory>
                  [--formats <ext,ext,…>]
                  [--session <directory>]
   ```

2. **Knowing a format when it sees one** — `carve::isBuiltinFormat`, so an
   allowlist entry no carver answers to is refused instead of ignored.
3. **The shared frontend layer**, extracted from story-0110 rather than copied:
   - `cli/RecoveryOptions` — the flags both binaries share (`--source`,
     `--destination`, `--session`), the "both paths are required" rule, and the
     session-directory default. Each grammar supplies only its own flags.
   - `cli/RecoveryRun` — story-0110's run, now driven by a `RunRequest` that
     carries the mode *and* the format allowlist.
   - `cli/Frontend` — `--help`, the parse, the run, and the words for what
     happened; a binary supplies its usage text and its grammar, nothing else.
4. **The binary** — `revenant-carve`, over `cli/CarveCli`.

## Design decisions

**Extract, don't copy.** Two frontends over one engine differ in exactly two
things: which flags they accept and what they print as usage. Everything else —
reading `--source`, refusing an unknown flag, defaulting the session directory,
opening the device, running, arbitrating, extracting, summarizing — is one
behaviour, and the duplication detector is right to forbid a second copy of it.
So story-0110's `UndeleteRun` becomes `RecoveryRun`, its options parser splits
into a shared reader plus an undelete-specific flag handler, and its `main`-side
plumbing becomes `runFrontend`.

**`revenant-carve` has no mode flag.** It is always carve-only, so there is
nothing to choose. `--fs-only` is refused as an unknown flag rather than accepted
and ignored: a tool that quietly does something other than what it was told is
the failure mode this project exists to avoid.

**An allowlist entry no carver answers to is a mistake, not a filter.**
`--formats tiff` looks right and is wrong — the RAW carver reports `tif` — and
the allowlist is applied at registration, so the mistake would produce a scan
that searched for nothing and reported success. `isBuiltinFormat` refuses it, and
`builtinFormatNames` puts the valid names in the binary's own usage text so the
operator does not have to guess.

**One list of format names, not three.** The names a frontend offers, the names
an allowlist may hold, and the names the registration matches against are one
piece of knowledge, so they are one table: the per-carver lists are flattened at
compile time into what `builtinFormatNames` returns. Restating them — in a second
array, or in a usage string — would mean a format added to a carver could quietly
stop being offered. A registry test pins the other direction: allowing every
offered name keeps every carver that ships.

**An empty `--formats` is refused too.** `--formats ""` states a restriction and
names nothing; treating it as "everything" would be the widest possible reading
of the narrowest possible instruction.

**One `RunRequest`, two grammars.** Both parsers produce the same request type
rather than a per-binary options struct plus a conversion. `revenant-undelete`
leaves the allowlist empty, which is already the documented "carve everything";
`revenant-carve` leaves the mode alone, because it only has one. Neither field is
speculative — each is set by one of the two grammars that exist.

**`--formats` stays off `revenant-undelete`.** A hybrid run carves too, so the
flag would be meaningful there — but nothing in M1 asks for it, and the surface
is trivial to widen later. The story that needs it can add it in one line.

## Acceptance criteria

### `carve::builtinFormatNames` / `isBuiltinFormat`

- [x] True for every extension a built-in carver reports (`jpg`, `png`, `mp4`,
      `mov`, `cr2`, `nef`, `arw`, `tif`, `zip`, `docx`, `xlsx`, `pptx`, `pdf`).
- [x] False for a plausible-looking name no carver answers to (`tiff`, `jpeg`).
- [x] False for the empty string.
- [x] Allowing every name `builtinFormatNames` offers registers every carver
      that ships — the two lists are provably the same one.

### `parseCarveOptions`

- [x] `--source` and `--destination` are required; the mode is always
      `kCarveOnly`.
- [x] Without `--formats`, the allowlist is empty — every format is carved.
- [x] `--formats jpg,png` yields exactly those two.
- [x] An unknown format name is `kInvalidArgument`.
- [x] An empty `--formats`, or one with an empty entry, is `kInvalidArgument`.
- [x] A repeated `--formats` is `kInvalidArgument`.
- [x] A mode flag is refused: `revenant-carve` has only one mode.
- [x] `--session` behaves exactly as it does for `revenant-undelete`.

### `revenant-carve`

- [x] Carves the fixture image into `carved/`, reconstructing no names.
- [x] `--formats jpg` still recovers the JPEGs.
- [x] `--formats pdf` recovers nothing from a JPEG-only image, and still
      succeeds — an empty recovery is an answer, not a failure.
- [x] `--help` prints the usage and succeeds; a refused command line prints it
      to stderr and fails.

### The extraction

- [x] `revenant-undelete` behaves exactly as it did in story-0110; its tests are
      unchanged apart from the parser's new return type.
- [x] No duplicated frontend logic (jscpd clean at the 8-line threshold).

## Test plan

Unit (`tests/unit/carve/CarverRegistryTest.cpp`): `isBuiltinFormat` over every
shipped extension, over near-miss names, and over the empty string; and the
offered-names-keep-every-carver check that pins the flattened table to the
per-carver lists.

Unit (`tests/unit/cli/CarveOptionsTest.cpp`): the minimal command line; the mode
is always carve-only; the default empty allowlist; a two-format list; an unknown
format; an empty list and an empty entry; a repeated `--formats`; a mode flag; the
shared session rules.

Integration (`tests/integration/CarveCliTest.cpp`): the binary driven over the
story-0118 fixture image — carved JPEGs under `carved/jpg/` and no reconstructed
tree, with `--formats jpg` and with a format the image does not contain, plus the
usage and `--help` surface.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Coverage held or raised (>= 85% core) — enforced by the CI coverage job;
      the instrumentation is GCC/Clang-only, so it is not reproducible on the
      local MSVC build.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `README.md` usage updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
