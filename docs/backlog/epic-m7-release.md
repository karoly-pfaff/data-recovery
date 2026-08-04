<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M7 — 1.0 release

**Goal:** ship it. The toolkit is feature-complete, fast and hardened by the time this
milestone opens; what remains is making it something a stranger can install, understand,
and trust — and then tagging it.

**Milestone:** [M7](../roadmap.md#m7--10-release)

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
| story-0701 | Windows + Linux packaging (archives, `.deb`, checksums) | M |
| story-0702 | User documentation, recovery playbook, and an honest limits page | M |
| story-0703 | 1.0.0 release checklist & tag | S |
| story-0704 | ADR-0012 records the two-tier destination rule, and ADR-0005 becomes immutable again | S |
| story-0705 | The CLI surface is stated once: `--help` renders from the table the parser reads | M |
| story-0706 | The gates measure the Python in `tools/`, and the 763-line seed generator is split | M |
| story-0707 | A gate that inspected nothing fails: the vacuity refusal moves into `gate_files` | S |

story-0704 through story-0707 come from the
[M6 architecture audit](epic-m6-loose-ends.md#milestone-architecture-audit) and are
described under [Stories added by the M6 architecture audit](#stories-added-by-the-m6-architecture-audit).
**story-0704 and story-0705 precede story-0702**, which cannot be written correctly
before them: the limits page is written off ADR-0005 and ADR-0011, whose authority the
audit found ambiguous, and the man pages are written off a flag list that is already
wrong.

## What each story is

**story-0701 — packaging.** CPack: a portable ZIP on Windows, a `.tar.gz` and a `.deb` on
Linux, each carrying both binaries, `LICENSE`, the man pages, and a checksum file.
`librevenant`'s only third-party dependency is GoogleTest, which is test-only, so the
shipped binaries need no runtime packages — worth stating in the story, because it is
what makes a plain archive an honest deliverable. It consumes the `build-release`
artifact [M5](epic-m5-performance.md) introduced rather than adding a build of its own.
Acceptance includes actually installing the `.deb` and running the installed binary, not
merely producing a file. Code signing is out of scope, and the release notes say so.

**story-0702 — documentation.** `--help` audited for completeness (every flag, every exit
code), hand-written man pages, a usage guide, and the recovery playbook: what to do when
the SD card will not mount, in the order a frightened user should do it — image first,
work on the copy, `--list-partitions`, `--dry-run`, verify the manifest. Plus
`docs/limitations.md`, an honest page on what Revenant cannot do: fragmented files,
encrypted volumes, TRIMmed SSDs, and — until [M8](epic-m8-acquisition-damaged-media.md) —
acquiring a failing drive, which is why the playbook's first step still points at
`ddrescue`. The gate that keeps documentation from rotting is mechanical: a test parses
`--help` and fails if a flag is missing from the man page.

**story-0703 — the release.** Version to 1.0.0, `[Unreleased]` closed with a date, the
tag, release notes carrying the benchmark numbers M5 produced, and the packages from
story-0701 attached. The first step of the release procedure in
[versioning.md](../versioning.md) — every gate green on `main` — is taken literally.

## Stories added by the M6 architecture audit

The boundary audit ([code-quality.md](../code-quality.md), run 2026-08-04 over
`v0.3.1..HEAD`; summary in [epic-m6](epic-m6-loose-ends.md#milestone-architecture-audit))
confirmed four findings adversarially and refuted four — three of the refuted ones were
layer-leakage claims, which is the audit's own headline: story-0613's gate held, and this
is the first milestone whose leakage answer is a clean **no**.

| Story | Finding it retires |
|-------|--------------------|
| story-0704 | **ADR-0011 is `Accepted` and false.** It still records the destination rule as "a lexical path-prefix comparison in `RecoverySink`" that "does not hold for raw-device sources", and names story-0609 as work that would make it true — work that landed at `4a4221e` inside this range. The rule now lives in `recovery/DestinationRule` as two tiers over `DeviceIdentity`. Worse, that decision was written into ADR-0005's Consequences *in place* (+14/−2), which [ADR-0001](../architecture/adr/adr-0001-record-architecture-decisions.md) forbids and which the ADR index added in the same increment restates. There is no immutable record of the new rule, and no trace that the old one was replaced. |
| story-0705 | **The CLI surface is owned in four places and restated in three more**, and has already drifted: `--help` is a real accepted flag (`src/cli/Frontend.cpp:25`) that neither usage text documents, and `--force-portable` is in both help texts but not in `docs/usage.md`. story-0702's planned gate compares `--help` to the man page, so it cannot see a flag the parser accepts and the help omits — the drift that exists today is exactly what it would pass. The fix pattern is already in the same file: `usage()` renders format names from the carve layer so the help cannot offer a name the allowlist would refuse. |
| story-0706 | **`tools/` is passed to gates 3 and 4 that measure only its C++.** `SOURCE_SUFFIXES` admits `.cpp`/`.hpp` alone, and M6 moved 2,115 new lines of Python into that blind spot — 13 files/1,681 lines at `v0.3.1` to 28/3,796 at HEAD — including `tools/fuzz/make_seed_corpus.py` at 763 lines, three times the hard fail and the largest source file in the tree. The exclusion is deliberate and unit-tested, so widening it is an [AGENTS.md](../../AGENTS.md) §2 scope decision rather than a bug fix. |
| story-0707 | **The vacuity refusal is a convention, not a mechanism.** Five gate scripts each carry their own copy of "an empty file set fails"; `check_file_length.py` — which enforces the §2 headline number — has neither the guard nor a unit test, and is the only script in `tools/lint/` without one. Six instances of the class in one milestone. |

**story-0704 and story-0705 come before story-0702.** The limits page is written off
ADR-0005 and ADR-0011, and the man pages off the flag list; both sources are wrong today.

## Notes

- **A 1.0 is a promise about compatibility.** From this tag on,
  [versioning.md](../versioning.md) binds: CLI flags, the on-disk output layout, and
  recovery accuracy per supported format are the surface, and making recovery *worse* for
  a supported target counts as breaking. story-0702 is where that surface gets written
  down, which is the real reason it precedes the tag rather than trailing it.
- **What 1.0 does not claim** is as important as what it does. The limits page is not an
  apology; a recovery tool that overstates itself costs somebody their photographs.
- **Limits other stories have already found**, for story-0702 to write down rather than
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
