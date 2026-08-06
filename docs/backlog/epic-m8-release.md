<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M8 — 1.0 release

**Goal:** ship it. The toolkit is feature-complete, fast, and — after
[M7](epic-m7-hardening.md) — describes itself accurately; what remains is making it
something a stranger can install, understand, and trust, and then tagging it.

**Milestone:** [M8](../roadmap.md#m8--10-release)

## Outcome / definition of ready-to-close

- Packages exist for Windows and Linux, and have been installed and run from the package
  rather than from a build tree.
- A user can find out what every flag does without reading source, and what the tool
  *cannot* do without finding out the hard way.
- Every gate is green on `main` in CI.
- `1.0.0` is tagged, `CHANGELOG.md` is finalized, and the packages are attached to the
  release.

## Stories

| Story | Title | Size |
|-------|-------|:----:|
| story-0801 | Windows + Linux packaging (archives, `.deb`, checksums) | M |
| story-0802 | User documentation, recovery playbook, and an honest limits page | M |
| story-0803 | 1.0.0 release checklist & tag | S |

**This milestone depends on [M7](epic-m7-hardening.md) having closed**, and not merely by
convention. story-0802 writes the limits page off ADR-0005 and ADR-0011 and the man pages
off the CLI's flag list; the M6 architecture audit found the first two contradicting the
shipped code and the third already drifted. M7 is where those are made true. Writing 1.0's
documentation before it would be transcribing sources that are known to be wrong.

## What each story is

**story-0801 — packaging.** CPack: a portable ZIP on Windows, a `.tar.gz` and a `.deb` on
Linux, each carrying both binaries, `LICENSE`, the man pages, and a checksum file.
`librevenant`'s only third-party dependency is GoogleTest, which is test-only, so the
shipped binaries need no runtime packages — worth stating in the story, because it is
what makes a plain archive an honest deliverable. It consumes the `build-release`
artifact [M5](epic-m5-performance.md) introduced rather than adding a build of its own.
Acceptance includes actually installing the `.deb` and running the installed binary, not
merely producing a file. Code signing is out of scope, and the release notes say so.

**story-0802 — documentation.** `--help` audited for completeness (every flag, every exit
code), hand-written man pages, a usage guide, and the recovery playbook: what to do when
the SD card will not mount, in the order a frightened user should do it — image first,
work on the copy, `--list-partitions`, `--dry-run`, verify the manifest. Plus
`docs/limitations.md`, an honest page on what Revenant cannot do: fragmented files,
encrypted volumes, TRIMmed SSDs, and — until
[M9](epic-m9-acquisition-damaged-media.md) — acquiring a failing drive, which is why the
playbook's first step still points at `ddrescue`.

The gate that keeps documentation from rotting is mechanical: a test parses `--help` and
fails if a flag is missing from the man page. **That gate is the second half of a pair**:
it cannot see a flag the parser accepts and the help text omits, which is the drift that
exists today. [story-0702](stories/story-0702-one-flag-table.md) supplies the first half
by making the help text render from the table the parser reads.

**story-0803 — the release.** Version to 1.0.0, `[Unreleased]` closed with a date, the
tag, release notes carrying the benchmark numbers M5 produced, and the packages from
story-0801 attached. The first step of the release procedure in
[versioning.md](../versioning.md) — every gate green on `main` — is taken literally.

**It also builds the automation that does not exist.** Found while tagging `v0.4.0`:
`ci.yml` runs on `main` and on pull requests, never on tags, and no job creates a GitHub
Release — `build-release`'s `upload-artifact` makes an expiring workflow artifact, not a
release download. **No release has ever been published from this repository**, and four
tags exist. So this story owns the tag-triggered workflow as well as the checklist, and
`docs/versioning.md` step 4 — which claimed the automation was already there — is corrected
as of `v0.4.0` rather than left to mislead whoever runs the 1.0 release.

## Notes

- **A 1.0 is a promise about compatibility.** From this tag on,
  [versioning.md](../versioning.md) binds: CLI flags, the on-disk output layout, and
  recovery accuracy per supported format are the surface, and making recovery *worse* for
  a supported target counts as breaking. story-0802 is where that surface gets written
  down, which is the real reason it precedes the tag rather than trailing it.
- **What 1.0 does not claim** is as important as what it does. The limits page is not an
  apology; a recovery tool that overstates itself costs somebody their photographs.
- **Limits other stories have already found**, for story-0802 to write down rather than
  rediscover. The destination-on-source check ([story-0609](stories/story-0609-destination-on-source-refused.md))
  compares physical storage, and there are two containers it does not see through: on
  Windows, a volume inside a Storage Space or a mounted VHD reports the *virtual* disk's
  extents, so a destination there is not recognised as sitting on the disk that holds it;
  on Linux, a loop-mounted image is reported as a disk of its own rather than as the file
  it is, so a destination inside an image that lives on the source disk is likewise not
  caught. Both are the same shape — output written into a container whose backing store is
  on the source — and both are outside what the story could reach. Neither is a silent
  wrong answer about ordinary storage; both are cases where the check answers about the
  container instead of about what carries it.
- **Two more limits M6 recorded**, both from [story-0606](stories/story-0606-soak-and-long-fuzz.md):
  the fuzz campaign was 14.5 CPU-hours rather than the 112 first scoped, so bugs behind a
  multi-stage input are unexplored; and CodeQL's C++ library declares no flow-source model
  for this tree's read primitives ([story-0615](stories/story-0615-codeql-code-scanning.md)),
  so static taint analysis covers the parsers' arithmetic and not the device boundary.
