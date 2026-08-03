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
- **One fuzz seed is not what its generator makes.**
  [story-0606](stories/story-0606-soak-and-long-fuzz.md) made
  `tools/fuzz/make_seed_corpus.py` regenerate every tracked corpus seed — the promise
  `.gitignore` has always made — and found one it does not:
  `Ext4EnumerateFuzz/volume.bin` differs at offsets `0x5000` and `0x500C`, `0x0C`
  tracked against `0x02` generated, the shape of an ext4 directory entry's `name_len`
  or `file_type`. The seed last moved in `74a5fd6`, so the drift predates that story
  and only became visible when something finally ran the generator to check. One of
  the two is stale; deciding which means reading `ext4_volume()` against
  `Ext4EnumerateFuzz`'s hard-coded geometry, which is a story, not a guess taken in
  passing. The fuzz target is not broken either way — it seeds from a valid ext4
  volume — so this is an inconsistency to resolve, not a defect to rush.
