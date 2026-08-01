<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Changelog

All notable changes to Revenant are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
See [`docs/versioning.md`](docs/versioning.md).

## [Unreleased]

### Fixed
- **A recovery run can no longer write onto the disk it is recovering**
  (story-0609). The only guard on the destination was a comparison of path
  *spellings*, written when every source was an image file. A raw device shares
  no path element with anything — `\\.\PhysicalDrive0` does not prefix
  `C:\recovered`, `/dev/sda` does not prefix `/mnt/out` — so a run pointed at a
  whole disk with its output on a volume of that same disk passed validation and
  wrote every recovered artifact onto the unallocated clusters it was reading
  from: the one loss mode a read-only recovery tool exists to prevent, delivered
  by the tool itself. The destination is now compared to the source by physical
  identity before the first read — Windows volume disk extents against the
  source's disk number or extents; on Linux the destination is traced through
  `/proc/self/mountinfo` to the device its filesystem was mounted from, and a
  mapped or RAID device through the kernel's `slaves` links down to the disks
  underneath it. A filesystem's own device number is deliberately *not* used:
  btrfs and overlayfs report one no block device owns, and LVM, LUKS and md
  report a device of their own, so either would compare the destination against
  something that is not where its bytes are. A destination on a *sibling* volume
  of the same disk stays allowed on purpose: the loss mode is overwriting the
  clusters under recovery, and a sibling volume holds none of them. An
  image-file source keeps the path rule it always had, and a destination whose
  filesystem type holds no local storage — a network share, which ADR-0007
  permits, or a tmpfs — conflicts with nothing. Anything else that cannot be
  traced to a disk refuses the run rather than being assumed safe, with the OS's
  reason attached. The refusal has a code and a sentence of its own, so
  `kInvalidArgument` goes back to serving the name-collision failure alone.

### Added
- **The mount-table reader has a fuzz target** (story-0609).
  `/proc/self/mountinfo` is the kernel's own text rather than an attacker's, so
  it is not the threat model the format parsers face — but it is split, indexed
  past a separator and octal-unescaped entirely by hand, a defect in exactly
  that code was undefined behaviour, and a defect there does not surface as a
  crash but as a destination on the disk being recovered being allowed. A parser
  whose failures are silent is the one worth handing hostile bytes to.
- **A test now asserts the guarantee the tool rests on** (story-0614). A full
  recovery run hashes its source image before and after and fails if a single
  byte moved. The source was already read-only by construction — `BlockDevice`
  declares no write operation and every open asks the OS for read access only —
  but nothing checked it, so a regression that opened the source read-write
  would have passed the whole suite.

### Changed
- **The warning contract now covers the configurations the tool ships from**
  (story-0611). CI had one optimized build and it deliberately compiled no test
  code, and it had three Clang builds, every one of them Debug — so no test
  translation unit was ever compiled at `-O2 -Werror` anywhere, and Clang's
  optimizer had never seen this tree at all. The release job stops overriding
  the preset's own `REVENANT_BUILD_TESTS=ON`, and gains a second leg that
  compiles the same preset with Clang. Neither leg runs the suite: the debug
  legs already run it under ASan and UBSan, which is the stronger dynamic check,
  and what these buy is the compiler's opinion. Because a build that compiled
  nothing would deliver that vacuously — which is exactly how the override went
  unnoticed for a milestone — each leg now asserts the test binary exists
  afterwards. The published artifact is byte-for-byte what it was: one leg
  stages it, the other publishes nothing.
- **What the new configuration found on its first run is fixed, not silenced**
  (story-0611). Five `-Wnull-dereference` instances on GCC, across three test
  translation units. Two of them were the same four-line helper written twice
  under different names — a `std::string` built from a pair of
  `istreambuf_iterator`s, which GCC inlines `sbumpc` into and then cannot prove
  safe; those sites pump the stream buffer instead, the duplicate is deleted in
  favour of the shared helper that already existed, and a third copy of the same
  idiom elsewhere in the tests went with it. The other three were one helper
  that built a directory-entry fixture, sizing its buffer from the record-length
  *field* and then writing a header and a name into it — which for a record
  shorter than those need wrote past the end. The buffer is now sized to hold
  what is written, so the overrun cannot be stated, and the fixture writes
  through a checked accessor rather than an iterator. No warning was disabled
  and no suppression added.
- **The duplication gate is Python, and building Revenant needs no JavaScript
  toolchain** (story-0602). `jscpd` brought Node, npm, a lockfile and 119
  packages along for one check; `tools/lint/check_duplication.py` does the same
  job with `lizard`, which parses C++ and hashes *unified* tokens, so blocks
  that are the same code under different names are caught too. The threshold is
  60 tokens per copy — the median function in the scanned files is 62 — and a
  block counts only where every copy of it reaches a function body, because a
  run of layout constants hashes like any other and every byte parser here has
  one. `package.json` and
  `package-lock.json` are deleted, and the gate has a `duplication` target, so
  it runs beside the other gates locally instead of only in CI.
- **What the new gate found on its first run is gone from the code**
  (story-0602). Ten duplicated blocks, all removed rather than excused: four
  carvers each carried their own copy of the signature check, exFAT and FAT32 had
  byte-identical chain-to-extents readers, and the endian conversions asked one
  question in four places. One block pointed at something the detector could not
  see by itself — "a boot sector is 512 bytes", stated in six files — which is
  now one constant. No public interface changed and no behaviour with it.
- **The documentation gives each file one job** (story-0614). `README.md` is the
  map: what Revenant is, and where everything lives. The command-line reference
  moved out of it whole into `docs/usage.md`, `CONTRIBUTING.md` moved to the
  repository root where GitHub looks for it, and the decision records gained an
  index. Facts that were stated in several documents now have one owner and the
  rest link to it, which turned up six that had already drifted apart —
  including a required `cppcheck`
  gate that does not exist, a push policy that contradicted the pull-request
  workflow, and a complexity limit named after a measure nothing enforces. All
  are corrected, and no relative link in the repository is broken.
- **Overflow-checked arithmetic moved to `core/`** (story-0601). `safeMul32`,
  `safeMul64` and `safeAdd64` guard products and sums derived from untrusted
  on-disk numbers — a property of hostile bytes, not of filesystems — but they
  lived in `revenant::fs` because that is where their first caller was, and
  `volume/` had been calling upward across a layer boundary to reach them since
  M4. They now live at `src/core/SafeArith.hpp` in namespace `revenant`, still
  internal — no production code outside the library calls them, so nothing was
  promoted into the public include tree. No signature, no logic and no existing
  test changed, and the binaries differ by nothing an operator can observe.
  What did change is the coverage: the guards were always correct, but no test
  held either end of them. All three are now probed at the limit and one step
  past it, each boundary proved by watching the mutation fail.
- **The UTF-16 name decoder moved to `core/`, and no layer depends upward on
  another any more** (story-0608). Turning UTF-16LE code units into UTF-8 is
  arithmetic over two-byte numbers; it lived at `revenant::fs::decodeUtf16Name`
  only because NTFS needed it first, and since M4 `volume/` had been reaching up
  across a layer boundary to decode GPT partition labels with it — the last such
  edge in the tree, after story-0601 removed the other two. `DecodedName` and
  `decodeUtf16Name` are now `revenant/core/Utf16Name.hpp` in namespace
  `revenant`, and the `%XX` / `%uXXXX` escape spelling moved with them, because
  a partition label that reports `%uD834` and "not exact" is telling the
  operator the same thing an NTFS filename does. What stayed in `fs/` is the
  volume's own conventions: which decoder a volume's names need (`decodeRawName`
  for ext4's unenforced bytes), FAT's deletion marker, case bits and 8.3 field
  widths, and the rule about which bytes may reach a recovered path, now
  `src/fs/PathSafeByte.hpp`. No
  signature, no logic and no assertion changed; the twelve decoder tests moved
  whole and the suite is green unmodified. One thing did change, because the
  split would otherwise have broken it: `%` was reserved in one file and
  emitted in the same file, and the two halves now sit in different layers, so
  the sigil is named once where it is emitted and the reservation reads it from
  there. ADR-0010's two-way split is amended to the three-way one the code now
  has.

### Fixed
- **The format gate runs on Windows again** (story-0607). `format` and
  `format-check` used to hand clang-format every source file as a single
  command line; the tree outgrew Windows' 32,767-character `CreateProcess`
  limit, so both targets died of a launcher error before clang-format started —
  on every invocation, while Linux CI (whose limit is megabytes) noticed
  nothing. The targets now drive `tools/lint/check_format.py`, which discovers
  the file set at run time and spends it in batches under a stated character
  budget: growth adds invocations, not length. A violation still fails naming
  the file, a clean tree still passes, and a file set that matches nothing
  refuses to pass rather than passing vacuously. Verified on both development
  platforms; the batching, the verdict and the empty-set refusal are
  unit-tested next to the other gate tests.

## [0.3.1] - 2026-07-30

### Changed
- Development tooling only — nothing user-facing changed. The `revenant:*`
  agent plugin moved to plain `.claude/` project tooling and grew hooks
  (clang-format on every C++ edit, tidy-stamp invalidation on header edits,
  an AI-attribution commit guard), a `gate-runner` subagent that runs the
  local quality gates out of the main context, and `start-story` /
  `fuzz-campaign` lifecycle skills. The shipped binaries change only by
  their version stamp.

## [0.3.0] - 2026-07-30

### Added
- **An AVX2 prefilter behind a runtime check** (story-0503). The matcher's reject step —
  "could any signature begin at this position" — is now answered 32 positions at a time by
  a two-nibble vector lookup, with every survivor handed to the same exact comparison the
  portable path uses. The structure of the scan does not change; one step of it has two
  implementations, which is what makes identical output a claim that can be met rather than
  hoped for. The filter is conservative in one direction only: it may pass a byte no
  signature starts with, and may never drop one.
  **Measured at 1.59× against the portable matcher on the Linux runner** and 1.22× on the
  Windows workbench — the same fixture, one machine, back to back. `scan-throughput` goes
  from 1 234 to 2 186 MiB/s on the runner, executing 59% fewer instructions, and from 831
  to 1 041 MiB/s on the workbench.
  It lives in a single translation unit compiled with AVX2 alone, so the rest of the binary
  still runs on a CPU without it; the machines people run recovery tools on are old
  machines. `CPUID` is queried once, when the signature table is built, and the answer
  travels with the table — not per window, and not from a lazily initialized global that
  would become a data race the moment a scan is sharded across threads.
- **`--force-portable`** on `revenant-carve` and `revenant-undelete`, documented in
  `--help`. The benchmark needs it to run both paths on one machine, but the reason it
  stays is the operator's: if the fast path misbehaves on a particular CPU, the person
  whose photographs are on that disk needs a way to turn it off without waiting for a
  release.
- The differential test now compares **three** implementations — the story-0502 reference,
  the portable path and the fast path — over the same seeded randomized windows, and says
  out loud when the machine cannot run the third rather than passing quietly. The carve
  binary's golden test additionally recovers the fixture twice, with and without
  `--force-portable`, and gets the same bytes back both times.
- `scan-simd-vs-portable`, the fifth benchmark case and the only *ratio* in the suite: one
  fixture, one machine, two runs. A ratio divides the machine out, which is why it is the
  one time-based number worth gating on.
- **The benchmark suite, and the gate that reads it** (story-0501). `tools/perf/` is a
  Python harness that builds nothing: it is pointed at a release build directory — or at
  an unpacked CI artifact — generates its fixtures with `revenant-imagegen`, and drives
  the shipped binaries over them. Four cases (`scan-throughput`, `carve-validate`,
  `ntfs-enumerate`, `end-to-end-hybrid`), each a `--dry-run` so the whole engine runs and
  nothing measures the destination's disk. Measuring from *outside* the process is what
  makes peak memory free, instruction counting possible and real I/O visible; the median
  is the headline, the spread is the veto, and a case whose subprocess exits non-zero is
  a failure rather than a very fast run.
- The harness **refuses to report numbers from a build that was not optimized**. CMake
  now writes `build-info.json` into the build directory, and it travels with the
  binaries: a debug or sanitized binary's figures look exactly like real ones.
- Instruction counts under `valgrind --tool=cachegrind --cache-sim=no`, and *absent*
  rather than faked where valgrind cannot run — which means Windows, the one platform it
  has no port for.
- `tools/perf/compare_baseline.py` rules on two result files, at thresholds the suite
  measured rather than thresholds somebody liked: **5%** on instruction count (eighty
  times the 0.06% two runner machines actually disagreed by), **10%** on peak memory
  (twice the worst 5.3% observed, on the one case small enough for a single allocator
  arena to show), and **25%** on wall-clock rates, where two runners differed by 22.5%
  with a within-run spread of 0.8%. None of them has a command-line override: a threshold
  that can be passed on the command line is a threshold somebody will pass on the command
  line to turn a red run green. A drop the baseline's own spread already covers is not
  called a regression, a benchmark that disappears is a failure, and no baseline file
  lives in the repository — CI compares a pull request's run against `main`'s published
  run.
- CI: a **`build-release`** job that compiles the tree once and publishes the binaries,
  and a **`benchmarks`** job that consumes them and installs no compiler, no vcpkg and no
  CMake. Nine of the other jobs already build from scratch; M7's packaging consumes the
  same artifact, so the two milestones together add one build rather than three.
- `revenant-imagegen` grew the fixtures the suite needs: `carve <output> <size>` writes a
  header-dense corpus (a valid JPEG and an unparseable PNG header every 8 KiB, so a scan
  pays for a candidate it accepts and one it rejects in equal number), `disk <output>`
  writes the whole-disk MBR fixture, and `ntfs <output> [mft-records]` writes the fixture
  volume with a larger `$MFT` — seven files are enumerated faster than a process starts,
  and a rate measured over that measures process startup. The fixed 32-record volume every
  test asserts against is unchanged, byte for byte.
- MBR partition tables (story-0403): `volume::parseMbrSector` validates the
  four-entry table in sector 0 — every status byte, the signature, and the rule
  that nothing is partitioned at LBA 0, which together tell a real table from
  the boot sector of an unpartitioned volume — and `volume::readMbrPartitions`
  turns it into the byte ranges a `PartitionView` opens. An extended entry
  contributes the logical partitions of its EBR chain instead of itself, with
  the chain's two relative addresses read against the two *different* bases the
  format states them in. The walk is bounded by length and by revisit, so a
  corrupt or crafted chain ends and keeps what it found rather than spinning.
- Fuzz target `MbrFuzz`, driving the whole read — table parser and chain walk —
  over an in-memory device, seeded with a partitioned disk.
- GPT partition tables (story-0404): `volume::parseGptHeader` and
  `volume::parseGptEntry` read the GUID Partition Table, and
  `volume::readGptPartitions` turns a device's into byte ranges with the names
  and type GUIDs the entries carry. The header's own CRC32 is verified before
  any field behind it is believed, and the header is required to agree about the
  LBA it was found at — the one field that keeps the two copies from being
  interchangeable.
- **The backup table is actually used.** A disk whose primary header *or* whose
  primary entry array fails its checksum is answered from the copy in the last
  sector, and the result says so: a disk that needed its backup is a damaged
  disk, and silently substituting it would hide that.
- `volume::defersToGpt` — whether sector 0 says the real table is the GPT. True
  of a protective MBR and equally of a hybrid one, whose entries are only a
  curated subset of what its GPT holds.
- Fuzz target `GptFuzz`, driving the whole read — both copies — over an
  in-memory device, seeded with a checksummed GPT disk.
- One reading of a disk's layout whichever scheme wrote it (story-0405):
  `volume::readPartitionTable` asks sector 0 which scheme owns the disk and
  yields `Partition{startBytes, lengthBytes, number, label}` for either. A sector
  0 that will not parse does not end the enquiry — a wiped first sector is one of
  the commonest things a damaged disk has, and the GPT that survives it is two
  sectors away, so it is tried anyway.
- **`--list-partitions`** on `revenant-undelete` and `revenant-carve`: prints the
  scheme, the count, and one line per partition with its offset, length and a
  label naming the well-known MBR types and GPT type GUIDs. It writes nothing and
  needs no `--destination`, because finding out what is on a disk comes before
  deciding where to put it.
- **`--partition <n>`** on both frontends: the run happens inside that
  partition's byte window. Nothing below the CLI learns that partitions exist —
  a `PartitionView` is a `BlockDevice` like any other. A number the table does
  not carry is refused rather than quietly turned into a whole-disk run.
- A synthetic *whole disk* under `tools/imagegen/disk/`, carrying all four
  filesystem fixtures as MBR partitions aligned the way a real partitioner
  aligns them.
- The two I/O decorators the layer has promised since M0 (story-0402).
  `CachingDevice` holds a least-recently-used set of fixed-size **aligned**
  blocks, which turns the many small overlapping reads of parsing into one read
  per block — and, because every read it issues covers a whole block, satisfies
  the alignment a raw physical device demands without anything above it learning
  what a sector is.
- **Real disks and volumes** (story-0401): `RawDevice` opens
  `\\.\PhysicalDrive0`, `\\.\C:`, `/dev/sda` or `/dev/sda1` read-only, takes its
  size and sector size from the OS, and reads arbitrary byte ranges out of them
  — which a raw device will not do on its own. `AlignedRead.hpp` widens each
  request to whole sectors and slices the answer back down; an already-aligned
  request goes straight through to the caller's buffer. One class rather than
  the two the layer once named, because a disk and a volume differ only in the
  path an operator types.
- `openSource` is now the single place a source path becomes a device: a regular
  file opens as an image, anything else as a raw device. Both frontends take a
  device path anywhere they took an image.
- `ErrorCode::kPermissionDenied` — the OS refusing for want of privilege, told
  apart from the thing not being there, because the answers differ: *run it
  elevated* against *check the path*.
- A **folder is refused as a source** (story-0406, ADR-0007), with a sentence
  that names the fix rather than the failure: a share root, a mounted NFS or SMB
  path and a plain directory all expose only the files that are still there,
  while recovery reads the bytes underneath them. An image *on* a share is
  unaffected — it is a regular file, and the network is a latency problem rather
  than a capability one.
- `RetryingDevice` survives a drive that will not answer: a failing read is
  retried whole, then one sector at a time, and sectors that still will not come
  are handed back as zeros and recorded in `badRanges()` with adjacent ones
  merged. Abandoning the read instead would cost every file that merely *touches*
  a bad sector.

### Changed
- **The performance gate now requires the instruction count to corroborate a rate drop**
  before calling it a regression. It fired on this milestone's own work: `carve-validate`
  ran 6.9× slower on a runner while executing 16% *fewer* instructions than the baseline,
  twice in a row, with a 0.4% within-run spread, on code that measured unchanged on the
  workbench. That case re-reads the carve bound per candidate, so it moves gigabytes
  through the page cache and measures the host's memory bandwidth more than the program.
  Doing more work executes more instructions, so the accidental quadratic the rate gate
  exists to catch still fails it; what the rule trades away is a slowdown at an unchanged
  instruction count, which is indistinguishable from a busy machine in a measurement taken
  on a machine we do not own. Both observations are checked in as gate fixtures.
- **One pass over the window, not one per signature** (story-0502). The scanner's hottest
  loop made a full `std::ranges::search` over every window for every registered
  signature — seven passes over every byte of the device, growing with each format added,
  in a project where a carver is supposed to be cheap to add. It now visits each position
  once: `carve::SignatureTable`, built when a carver is registered and owned by
  `CarverRegistry`, answers "could anything begin with this byte" in one load and one
  test, which is the answer for almost every byte of almost every device, and the rare
  survivor goes to an exact comparison. Adding an eighth format costs the scan nothing per
  byte.
  **Measured on the Linux runner: `scan-throughput` 329 → 1 234 MiB/s (+275%), with 77.5%
  of the instructions gone** — 6.77 G down to 1.52 G. `carve-validate` +37.5% and
  `end-to-end-hybrid` +81%, both of which scan as well as validate. On the Windows
  workbench, whose standard library *does* vectorize its `search`, the same change is
  643 → 831 MiB/s (+29%).
  `ntfs-enumerate` is the control: its rate moved 28.5% and its instruction count did not
  move at all, which is two runner machines disagreeing rather than anything improving —
  and exactly why the gate holds the line on instruction count rather than on rates.
  The matcher this replaced is kept in `tests/support/ReferenceMatcher.cpp` as the oracle:
  a differential test asserts that both produce an identical `Match` sequence — same
  offsets, same carvers, same order — over randomized windows from a fixed seed, plus the
  cases that decide it (a magic at a non-zero in-file offset, two carvers claiming one
  candidate, overlapping occurrences of one magic, a magic in the window's first and last
  bytes, and a hit whose candidate start would underflow).
- The match order is now a *total* order — by candidate offset, then by the carver's
  registration position — rather than a sort by offset alone. Two candidates at one byte
  used to be ordered by however the window happened to be walked, and the walk is exactly
  what changed; everything downstream depends on that order, because a candidate falling
  inside an extent an earlier one resumed past is skipped.
- The per-window match list is a buffer the scan reuses rather than a vector returned by
  value, so the hot loop allocates nothing beyond that list's own growth (ADR-0009).

### Fixed
- The tree now builds clean with GCC **at `-O2`**, which nothing had ever tried:
  every Linux job so far compiled it in Debug, and two of the project's own warnings
  only fire once the optimizer runs. `Result::map` and `Result::andThen` branch on the
  pointer `std::get_if` returns rather than on `hasValue()` — the same question, but
  only the first one is a question GCC's optimizer can answer, and asking it the other
  way left an unguarded dereference under `-Wnull-dereference`. The GPT header's
  checksum field is now *located* rather than assumed present: `withoutChecksum` zeroed
  four bytes at a fixed offset into a copy whose length it had not checked, which
  `-Warray-bounds` reported and which would have been undefined behaviour had a caller
  ever passed a short header. Both found by the new `build-release` job.

## [0.2.0] - 2026-07-29

Milestone M3, filesystem breadth, closed. `revenant-undelete` now reads four
filesystems behind one seam: NTFS, FAT32, exFAT and ext4. Each is offered the
volume in turn and the first to recognize its own signature owns the answer.

What a deletion leaves behind differs at every one of them, and the entries
report that difference rather than papering over it. NTFS keeps the record and
gives a file back exactly. exFAT clears one bit and hands back the whole name,
with a *stated* extent where the file was contiguous. FAT32 takes the first
character of the name and frees the chain, so its extents are the contiguity
guess that is all a freed chain leaves. ext4 does not mark the deletion at
all — the previous entry's record simply grows over it — so its names are
*searched for*, and where the deletion also wiped the inode's extent tree, the
journal is asked whether it still remembers where the bytes were.

### Added
- `fs::FileSystem`: the filesystem seam M1 deferred — a mounted volume with one
  method, `enumerate`, and `fs::EnumerationStats` promoted out of `fs::ntfs` as the
  shared shape every filesystem reports (story-0301).
- `fs::mountVolume`: one factory that offers a volume to every filesystem this build
  can read, in a fixed probe order, and hands back the first that recognizes it.
  NTFS now arrives behind it like any other filesystem; nothing under
  `src/recovery/` names a filesystem any more.

- FAT32 geometry: `fs::fat::parseFat32BootSector` validates the BPB field by field
  — each rejection naming the byte offset that caused it — and derives where the
  FATs and the data region are, how many clusters the volume holds, and where the
  root directory starts (story-0302).
- FAT32 directory entries: `fs::fat::classifyEntry`, `parseShortEntry` and
  `parseLongNameFragment` read one 32-byte slot. A `0xE5` deletion takes a name
  byte and leaves cluster, size and timestamps standing, which is what makes FAT
  undelete possible; the lost character comes back as `_` with the name marked
  approximate rather than guessed at. DOS date/time converts to the layer's
  FILETIME ticks, and an unreadable field yields no timestamp rather than a
  fabricated date.
- Fuzz targets `Fat32BootSectorFuzz` and `FatDirectoryEntryFuzz`, the latter
  asserting that every name decoded off a slot is valid UTF-8.
- **FAT32 recovery, end to end** (story-0303): `fs::mountVolume` now mounts a FAT32
  volume and walks its directory tree, reporting live, deleted and orphaned files
  with their long names, their place in the tree, and extents that hold their
  bytes. A live file's content is read through its FAT chain, so a fragmented file
  comes back exactly; a *deleted* file's chain was freed on deletion, so its
  extents are the contiguous run its size needs — the only guess left — and it is
  graded `kUncertain` because of it. Files under a deleted directory come back as
  orphans: their names are real, their place in the tree is not.
- A synthetic FAT32 image builder under `tools/imagegen/fat/`, holding a live
  fragmented file with a long name, deleted files, and a deleted directory, plus
  the integration test that mounts it through the real front door.
- `Fat32EnumerateFuzz`: arbitrary bytes mounted and walked, so a crafted FAT cycle
  or directory loop cannot hang a scan.
- exFAT boot region: `fs::exfat::parseExfatBootSector` validates the main boot
  sector and derives the volume's geometry. exFAT states sector and cluster size
  as log2 exponents, so each exponent is range-checked *before* anything is
  shifted by it — an unchecked shift is undefined behaviour, not a large number.
  Recognition is the `EXFAT   ` name *and* the 53 zero bytes exFAT deliberately
  writes where a FAT BPB keeps its geometry (story-0304).
- exFAT directory entries: `fs::exfat::classifyExfatEntry`, `parseFileEntry`,
  `parseStreamExtension` and `parseFileName` read one 32-byte slot. Deleting a
  set clears bit 7 of every type byte in it and touches nothing else — which is
  why exFAT hands a deleted file its whole name back, and FAT32 does not. A
  stream extension's `NoFatChain` flag is reported, so a deleted contiguous
  file's extent will be a stated fact rather than the guess FAT32 forces.
- Fuzz targets `ExfatBootRegionFuzz` and `ExfatDirectoryEntryFuzz` with seeded
  corpora.
- **exFAT recovery, end to end** (story-0305): `fs::mountVolume` mounts an exFAT
  volume — before FAT32, since an exFAT volume also carries a FAT-shaped boot
  sector — assembles the entry sets a file is spread across, and walks the tree.
  A deleted file keeps its whole name, and a deleted *contiguous* file states
  where its bytes are instead of guessing, which is what exFAT can do and FAT32
  cannot.
- exFAT allocation bitmap: a deleted set whose clusters the volume has since
  handed out again is reported with **no extents** — its name is real, but its
  bytes are not, and handing back a live file's data would be worse than
  handing back none.
- A synthetic exFAT image builder under `tools/imagegen/exfat/`, plus the
  integration test that mounts it through the real front door.
- ext4 superblock: `fs::ext4::parseExt4Superblock` validates the superblock —
  which sits a kilobyte into the volume, not in sector 0 — and derives the
  volume's geometry. The checks are ext4's *own* consistency rules rather than
  constants: blocks and inodes per group are capped by what one bitmap block can
  address, and an inode and a group descriptor must each fit inside a block.
  Recognition is the `0xEF53` magic **and** a block-size shift ext4 can express;
  sixteen bits of magic alone is a coincidence a RAW volume can produce, and a
  mounter that claims a volume owns its answer (story-0306).
- ext4 inodes and extent trees: `fs::ext4::parseExt4Inode`,
  `parseGroupDescriptor`, `parseExtentHeader`, `parseExtentLeaves` and
  `parseExtentIndices`. A freed inode keeps its mode, size, times and block map
  and loses only its link count — which is what makes ext4 undelete possible.
  `i_size_high` counts only for a regular file, because ext4 reuses it as
  `i_dir_acl` for everything else; `created` comes from `i_crtime` or from
  nowhere, never from `i_ctime`, which is not a creation time. An extent length
  above 32768 is a length *and* a flag: those blocks are allocated but unwritten,
  and a walk that missed it would pad a file with whatever they last held.
- ext4 directory entries: `fs::ext4::parseExt4DirEntry` reads one linear entry.
  A record length that is not a plausible distance to the next entry ends the
  walk instead of sending it somewhere arbitrary.
- `fs::decodeRawName`: the decoder ADR-0010 owes ext4, which stores names as raw
  bytes with no enforced encoding. Well-formed UTF-8 passes through unchanged —
  so this is a validation rather than a transcoding — and everything else is
  escaped `%XX` one byte at a time: an invalid, truncated or overlong sequence,
  a surrogate, a NUL or control byte, and `/` and `%`.
- Fuzz targets `Ext4SuperblockFuzz`, `Ext4InodeFuzz`, `Ext4ExtentTreeFuzz` and
  `Ext4DirectoryEntryFuzz` with seeded corpora, the last asserting every name
  decoded off an entry is valid UTF-8.
- **ext4 recovery, end to end** (story-0307): `fs::mountVolume` mounts an ext4
  volume — last of the four, since sixteen bits of magic a kilobyte in is the
  weakest signature of them — follows each inode's extent tree onto the volume,
  and walks the directory tree from inode 2 down.
- ext4 deleted-entry recovery: ext4 does not *mark* a deletion, it adds the
  deleted entry's record length to the **previous** entry's so that record
  swallows it. Deleted names are therefore found by searching the hole behind
  each live entry, and a candidate has to name an inode the volume could have,
  fit inside the record, and carry bytes a name can be made of before it is
  believed — and is graded `kUncertain` even then.
- ext4 journal hint: many kernels zero an inode's extent tree when they free it,
  leaving the name recoverable and the blocks not. The jbd2 journal is indexed at
  mount and searched for an older copy of the inode's own table block; the first
  copy whose tree still points somewhere supplies the extents. The journal is
  **read, never replayed** (ADR-0005), and one carrying a feature that changes
  its descriptor-tag layout is declined whole rather than guessed at.
- ext4 orphan list: an inode unlinked while still open has no directory entry
  anywhere and no name to recover, so it comes back as `#<inode>`, graded
  `kOrphaned`. The chain runs through each orphan's `i_dtime` and is bounded,
  cycle-checked, and refused a number the volume could not have.
- A deleted ext4 name whose inode has since been handed back out is reported
  with **no extents** — the name is a fact, the bytes behind it are not.
- A synthetic ext4 image builder under `tools/imagegen/ext4/`, holding a live
  fragmented file, a file in a subdirectory, a deletion with its tree intact, one
  whose tree was wiped and whose journal copy survives, a name whose inode was
  reused, and an orphan — plus the integration test that mounts it through the
  real front door.
- Fuzz targets `Ext4EnumerateFuzz` and `Ext4JournalFuzz` with seeded corpora, so
  no crafted extent tree, directory record, orphan chain or journal can hang a
  scan. The bounds they hold are on memory as well as on time: an extent tree's
  node budget covers the children *queued* as well as those visited, a file may
  name at most 65 536 runs, and a directory's read cap cuts a single over-long
  extent inside itself rather than after allocating it.
- `fs::ClusterChain` and `fs/DirectoryTreeWalk.hpp`: chain following, directory
  reading, worklist driving, slot folding and path joining now live once rather
  than once per filesystem. The duplication gate found each of them.
- `fs/ExtentSpan.hpp`: coalescing located runs and trimming them to a file's own
  size is the same operation whether NTFS runlists, FAT chains or ext4 extent
  trees produced them, and is now written once.

### Changed
- The shared mount-region read grew an offset and became `readMountRegion`:
  every filesystem before ext4 names itself in sector 0, and ext4 does not.
- Unix-seconds-to-FILETIME conversion moves out of FAT's DOS-time code into
  `src/fs/UnixTime`, where ext4 is its second caller, and the rule that a name
  byte may pass through as itself into `src/fs/NameEscape`, where the raw-byte
  decoder is.
- A run whose volume is not what a conforming formatter writes now says so on its
  discovery line. FAT32's 65525-cluster minimum is the first such rule: a volume
  below it is malformed, but it is still readable, and refusing it would throw
  away files that are plainly there — so the operator gets a warning instead of a
  refusal. The fact travels from the parser out through `fs::EnumerationStats` and
  `recovery::RecoveryStats`, the path `filesystemMounted` already took.
- NTFS's overflow-checked arithmetic, its shared BIOS-parameter-block field
  readers, and the ADR-0010 name escape are no longer private to NTFS: they move
  to `src/fs/SafeArith`, `src/fs/BpbFields` and `src/fs/NameEscape`, where FAT32
  is their second caller. NTFS's boot sector *is* a BPB, so its sector size,
  cluster size and boot signature were the same rules written twice.
- A volume no filesystem recognizes now fails with `kNotFound` rather than
  `kInvalidArgument`. "Nothing recognized this" is a different fact from "this NTFS
  volume is broken", and it is the one a formatted or RAW volume presents. A
  mounter that *does* recognize its own signature still owns the answer: its parse
  failure is reported unchanged instead of being passed to the next filesystem.
  Hybrid runs are unaffected — an unmountable volume still downgrades to carving.

## [0.1.0] - 2026-07-28

First tagged pre-release: milestone M1, the vertical slice. A deleted, fragmented
JPEG on a synthetic NTFS volume comes back at its own path with its own bytes,
alongside one carved out of unallocated space that no record points at — through
two real binaries, from one pass over the device, with a manifest that vouches
for every byte.

### Added
- Project foundation: engineering contract (`AGENTS.md`), agent guide (`CLAUDE.md`),
  and repository documentation set under `docs/`.
- Architecture documentation: layered design, I/O abstraction, filesystem parsers,
  carving engine, and hybrid orchestration, plus initial ADRs.
- Roadmap (M0–M5) and backlog structure (epics + stories).
- Testing strategy and quality-gate definitions; versioning, code-quality, and
  performance standards.
- Toolchain configuration: CMake + vcpkg, CMake presets, `.clang-format`,
  `.clang-tidy`, `.editorconfig`, and CI workflow (Windows + Linux, sanitizers,
  coverage, duplication detection, file-length guard, fuzz smoke).
- `revenant:add-format-carver` project skill for adding new carve formats.
- Trunk-based git workflow (`docs/git-workflow.md`): one `story/*` branch per story,
  squash-merged to `main`; epics/milestones as labels, not branches.
- Design decisions captured as ADRs 0006–0010: candidate arbitration & deferred
  extraction, block-level (incl. network) access boundary, resumable checkpointing,
  output safety (path confinement + bounded allocation), and filename decoding.
- `docs/architecture/recovery-output.md`: session manifest (provenance + SHA-256 +
  bad-sector map), `--dry-run` preview, and destination scaling.
- Backlog stories for output safety, filename decoding, manifest, dry-run, resumable
  scan (M1), plus imaging mode and network block device (M4).
- Build activation: static `librevenant` with `revenant::version()`, GoogleTest
  test target (`revenant_tests`), and working CI build/test jobs on Windows and
  Linux (MSVC dev env, vcpkg bootstrap, MSVC-ASan `/RTC1` fix, coverage test
  preset, duplication gate made enforcing).
- Core primitives: typed `Error`/`ErrorCode`, `Result<T>` (value-or-error with
  `map`), explicit-endianness integer readers (`fromLittleEndian`/`fromBigEndian`
  via `std::bit_cast`), and bounds-checked `ByteReader` with a libFuzzer target
  wired into CI's fuzz-smoke job.
- Leveled logging facility: `LogLevel`, injectable `LogSink` seam, `Logger`
  with threshold filtering, and a `StderrSink` for CLI tools.
- `BlockDevice`: the read-only, random-access I/O seam every layer reads
  through (no write operation exists on the interface), plus the
  `InMemoryDevice` test double backing unit tests.
- `ImageFileDevice`: portable read-only image reader (`.dd`/`.img`) with
  positioned, thread-safe reads (`pread` / overlapped `ReadFile`), typed
  `IoError`s carrying offset and OS code, platform code selected by CMake.
- Coverage gate: `tools/lint/check_coverage.py` enforces the 85% core-logic
  line-coverage floor from real llvm-cov data in CI (empty matches fail the
  gate); the checker itself is exercised by ctest fixture cases.
- `revenant-imagegen`: deterministic synthetic-image generator scaffold
  (zero / counter / LBA-tag patterns, exact byte sizes) — the seed of the
  test-image corpus; `toLittleEndian` added to the core endian helpers.
- Carve layer seams: `FormatCarver` interface, owning `CarverRegistry`, and
  the bounded-window streaming `SignatureScanner` that reports
  verdict-carrying candidates to a visitor (discovery only — extraction is
  deferred to arbitration per ADR-0006), plus the cross-layer `Confidence`
  verdict scale.
- JPEG validating carver: exact SOI→EOI extents via marker-structure walking
  (segment lengths bounds-checked against the input, byte-stuffing, restart
  markers, progressive JPEGs' multiple SOS scans) with
  Valid/Uncertain/Rejected verdicts, an exact `Uncertain` extent on truncated
  input (entropy exhaustion is a reported value, not a discarded error) —
  the validating-carving thesis proven end to end with golden-file
  byte-identity tests; registered via `registerBuiltinCarvers`.
- Output safety (ADR-0009): `sanitizeOutputPath` path-confinement choke point and the
  `boundedCount` allocation guard, both fuzz-tested.
- NTFS filename decoding (ADR-0010): lossless UTF-16→UTF-8 with escaping for
  undecodable units, plus deterministic output-name disambiguation.
- `PartitionView`: byte-range partition window over any `BlockDevice` — the fs
  layer's mount seam until real MBR/GPT parsing arrives (M4).
- `Result::andThen`: monadic bind for chaining `Result<T>` through error-preserving
  transformations (used by the NTFS boot-sector parser).
- `NtfsGeometry` + `parseBootSector`: validated NTFS boot-sector parser with
  per-field typed rejections, producing cluster size, MFT byte offset, and
  MFT record size.
- `MftRecordView` + `parseMftRecord`: validated NTFS MFT record parser with
  update-sequence fixup, attribute header parsing, resident/non-resident
  attribute distinction, and extraction of `$STANDARD_INFORMATION`,
  `$FILE_NAME`, and resident `$DATA` attributes. Non-resident `$DATA` runlist
  bytes are captured for the runlist decoder (story-0105).
- `decodeRunlist` + `runlistExtents`: NTFS `$DATA` runlist decoder. Data runs are
  walked structurally (nibble-encoded field widths, unsigned cluster counts,
  signed LCN deltas, sparse runs) with typed rejections for malformed widths,
  zero-length runs, deltas driving the LCN negative, cluster-total overflow, a
  missing end marker, and a run count past `kMaxDataRuns` (ADR-0009). Mapping to
  device byte extents is a separate, geometry-aware step that validates runs
  against the volume and trims the tail to the attribute's declared size —
  turning MFT metadata into the byte ranges a deleted file's content lives in.
  Sparse `$DATA` is decoded faithfully but refused by the extent mapper, so such
  a file goes to the carve pass rather than being reassembled wrongly.
- Carve-format allowlist and plausibility floor, closing M2. The allowlist is
  applied at registration, so a format the user did not ask for costs nothing —
  not a signature search, not a carve attempt. The floor rejects a match that is
  structurally perfect but far too small to be a real file of its kind, which is
  what a disk full of random data produces; a format with no shipped carver gets
  no floor, because any number chosen for it would be invention. The golden JPEG
  fixture grew to a realistic size rather than the filter being bent around it.
- PDF validating carver: the file ends at its **last** `%%EOF`, not its first —
  an incrementally saved PDF carries one marker per revision, and stopping at
  the first silently discards every later one. The `startxref` offset behind
  that marker is parsed from a bounded window and resolved against the file's
  own bytes (a classic `xref` table or an indirect object holding a
  cross-reference stream), so a `%%EOF` that is merely a string in the data is
  reported as `Uncertain` rather than vouched for. The end-of-line bytes after
  the marker belong to the file; nothing else does. Fuzz-tested.
- ZIP validating carver: the extent comes from the End Of Central Directory
  record, and the record is *checked* rather than merely found — a real archive
  satisfies `centralDirectoryOffset + size == eocdOffset` and has a directory
  header at that offset, so a stray `PK` in the data cannot end the
  file early. The last end record wins, since an archive may legitimately
  contain another archive's bytes. Office documents are ZIP archives, so the
  entry names name them: docx, xlsx, pptx, else zip. Fuzz-tested.
- Camera-RAW validating carver: RAW files are TIFF containers, so the extent
  comes from walking the IFD chain and taking the highest offset anything in the
  file points at — each IFD table, every out-of-line entry value, and the image
  data located by the strip or tile tag pairs, which is normally the file's last
  and largest part. Both byte orders are read in the file's own order; the IFD
  chain is capped because a `next` pointer may point backwards; the extension
  follows Canon's header marker or the `Make` tag (cr2/nef/arw/tif).
  Fuzz-tested.
- MP4/MOV validating carver: walks the top-level box list from `ftyp`, summing
  box sizes (both the 32-bit and the 64-bit `largesize` form) to the exact
  extent. A box size below its own header, a size running past the data, a
  size-0 "to end of file" box — whose extent a carve candidate cannot know — or
  a box type that is not four printable ASCII characters ends the walk as
  `Uncertain`; `ftyp` plus `moov` plus `mdat` is `Valid`. The `ftyp` major brand
  picks the extension, so QuickTime files come back as `.mov`. Fuzz-tested.
- PNG validating carver: walks the chunk list from the 8-byte signature through
  `IHDR` to `IEND`, verifying every chunk's CRC-32, and reports the exact extent.
  A failed CRC, a truncation, or a chunk length running past the data ends the
  trusted prefix as `Uncertain` instead of being waved through; bytes whose first
  chunk is not `IHDR` are `Rejected`. Fuzz-tested.
- `revenant::crc32`: IEEE 802.3 CRC-32 in core, pinned to the published check
  vectors — shared by the PNG chunk walk and (later) ZIP entries.
- NTFS synthetic-image builder in `tools/imagegen`: a fixed 4 MiB fixture volume
  with a real boot sector, a 32-record `$MFT`, and a directory tree holding live,
  deleted, and orphaned files — including a **fragmented** deleted JPEG, a
  deleted file with resident `$DATA`, and one JPEG in unallocated space that no
  record points at. Built from small single-purpose units (`NtfsLayout`,
  `BootSectorBuilder`, `RunlistEncoder` — the inverse of the story-0105 decoder —
  `AttributeBuilder`, `MftRecordBuilder`, `NtfsImageBuilder`), each specified
  against the production parser that reads it back. The integration test walks
  the generated image through `ImageFileDevice` → `parseBootSector` →
  `parseMftRecord` → `decodeRunlist` → `runlistExtents` and recovers every
  file's bytes identically, which is the M1 vertical slice proven end to end on
  real metadata.
- NTFS deleted-entry enumeration with path reconstruction — the filesystem half
  of the vertical slice, and the thing carving can never do: give a recovered
  file back its name and its place in the tree. `MftTable` treats the `$MFT` as
  what it is, a file, so opening it means reading record 0 and decoding the
  table's own runlist; a **fragmented** `$MFT` is then addressed through its
  extents rather than assumed contiguous, and a record straddling two of them is
  refused instead of stitched together from the wrong place. Path reconstruction
  walks `$FILE_NAME` parent references to the root, preferring the long name
  over the DOS 8.3 alias, and checks each parent's sequence number — a reused
  slot is a stale reference, not a directory. The walk is depth-bounded, so a
  parent cycle on a crafted volume terminates instead of hanging a scan.
  `enumerateEntries` reports every live, deleted, and orphaned file as a
  `RecoveredEntry` carrying its path, timestamps, and the byte extents its
  content lives in — discovery only, nothing extracted (ADR-0006). A slot that
  will not parse is skipped, because an empty or destroyed record is precisely
  what the carve pass exists for; a device read fault is not, and stops the walk
  as a typed error. Resident content is carried as bytes rather than as an
  extent: the on-disk copy is interrupted by the update-sequence fixup and is
  not the file's bytes. Fuzz-tested end to end (`NtfsEnumerateFuzz`), seeded
  with a whole synthetic `$MFT`.
- Hybrid orchestration (`HybridRecovery`): the two recovery sources in one run.
  The filesystem pass recovers what the metadata can name, byte accounting
  records what those names already speak for, and the carve pass searches only
  the gaps — which is where the claim "better than the sum of PhotoRec and
  TestDisk" actually lands: the fixture's JPEG in unallocated space comes back
  *and* the named files keep their paths, from a single pass over the device.
  A region bounds where a signature is *looked for*, never what a file may be:
  a candidate starting inside a gap is still carved to its true length, because
  the boundary belongs to whatever claimed the next bytes and truncating there
  would turn a whole recovery into a fragment. An `Uncertain` entry does not
  suppress carving; the architecture calls that region a safety net, and the
  fixture's orphan proves it is one. A volume that will not mount downgrades a
  hybrid run to carving rather than ending it — a formatted volume is exactly
  what carving is for — and the run says so rather than staying quiet.
- `RecoverySink`: the one place in the project that creates files, and the last
  step of the M1 vertical slice. Only arbitration's winners are written
  (ADR-0006): a named entry reconstructs the directory tree it had inside the
  volume, a carved one lands in `carved/<ext>/f<ordinal>.<ext>` numbered in
  device order, and every path passes through `sanitizeOutputPath` — there is
  no other way to derive one. A carver's extension is data, so it may name a
  bucket only if it looks like one of ours. Two winners wanting one path are
  renamed by the ADR-0010 rule and the rename is *counted*, because a rename is
  a fact about the output. A short read is a failed recovery rather than a
  smaller file that looks complete, and content is copied through a bounded
  scratch buffer so a 4 GiB video never becomes a 4 GiB allocation. The
  destination must exist, be a directory, and not contain the source: recovered
  data does not get written onto the media being recovered (ADR-0005).
  The end-to-end test now mounts the fixture image, recovers it by both
  sources, indexes, arbitrates, extracts, and compares every written file
  against the fixture that produced it — the deleted, fragmented JPEG back at
  `photos/deleted.jpg` byte-for-byte, and the JPEG no record points at back
  under `carved/jpg/`.
- File-backed candidate index and confidence arbitration (ADR-0006): discovery
  and extraction are now genuinely separate. Both recovery sources append what
  they find to one durable index in the session directory — fixed-size records
  plus a blob for names, extent lists and resident content — and the blob is
  written *before* the record that points at it, so a record can never refer to
  bytes that are not on disk and an interrupted run leaves a readable prefix
  rather than a corrupt file. Reading one back treats it as the untrusted data
  it is: a torn tail, a record pointing past the blob, or a length past its
  bound is dropped and counted, and a file that is not an index of this version
  is refused outright.
  `arbitrate` then resolves competing explanations of the same bytes. A
  filesystem entry beats a carve of its own region **ahead of** confidence,
  because the two scales measure different things: a carver grades the
  structure of the bytes in front of it, while a filesystem entry knows the
  name, the timestamps, and which runs the content is spread across — so a
  structurally perfect carve beginning at a fragmented file's first run would
  hand back garbage. A candidate wins whole or not at all; a partial overlap
  loses it, since accepting the rest would emit exactly the fragments
  arbitration exists to remove. Suppressed candidates are counted rather than
  dropped quietly, because "why is this file not in the output" is a question a
  recovery tool has to be able to answer.
- `RegionSet`: the "which bytes are spoken for" primitive behind both
  arbitration and byte accounting, which was refactored onto it — two
  implementations of one fusing rule was one too many.
- `ByteAccounting`: the accounted-region set behind the above. Overlapping and
  touching extents fuse, so it stays proportional to distinct regions rather
  than to file count, and it is capped and reports what it dropped (ADR-0009) —
  safe here in a way dropping a candidate would not be, since less accounting
  only ever means more scanning.
- `locateInExtents`: maps a file offset onto the device offset holding it,
  shared by every extent-based filesystem.
- `revenant-undelete`: the first real binary, and the first way to run any of
  this without a test harness. It maps flags onto the recovery layer and holds
  no policy of its own — `--fs-only`, `--hybrid` (the default) and `--carve-only`
  are `RecoveryMode`, `--source` and `--destination` are paths, and every
  decision about what to recover stays in `recovery/`. The run is the
  architecture's three steps in order (discover, arbitrate, extract), with the
  destination validated *before* the scan rather than after it: a run that cannot
  land anywhere should fail in its first second, not its last. Two contradictory
  mode flags are refused instead of resolved, and a candidate the index could not
  record fails the run outright — every count afterwards is read back out of that
  index, so one lost record makes the answer wrong rather than smaller. The run's
  durable state goes to `<destination>/.revenant` unless `--session` says
  otherwise. The frontend's logic lives in a static library the test binary drives
  directly, so the CLI is exercised by unit tests and by an end-to-end test per
  mode rather than only by hand.
- `revenant-carve`: the second binary, and the PhotoRec-shaped one — recovery
  from a volume nothing can mount, where structure alone decides what comes
  back. It is always carve-only, so it has no mode flag; `--fs-only` is refused
  rather than accepted and ignored. `--formats jpg,png` narrows the scan at
  *registration*, so an excluded format costs nothing at all, and a name no
  carver answers to is refused instead of obeyed: `--formats tiff` looks right
  and is wrong (the RAW carver reports `tif`), and obeying it would produce a
  scan that searched for nothing and reported success. The names a run may ask
  for, the names the registration matches, and the names the binary lists in its
  own help are now one compile-time table (`carve::builtinFormatNames`), so a
  format added to a carver cannot quietly stop being offered.
  Shipping it turned the frontend into a layer rather than one binary's private
  plumbing: `revenant-undelete`'s run, its shared flags, and its `--help`/parse/
  run/report plumbing moved into `cli/RecoveryRun`, `cli/RecoveryOptions` and
  `cli/Frontend`, so the two binaries differ in exactly what they should — which
  flags they accept, and what they print as usage.
- Session manifest: every run now leaves a `manifest.json` in its session
  directory, next to the candidate index — the durable record that makes a
  recovery auditable by someone who did not watch it happen. Per artifact it
  states provenance (a filesystem entry or a carve), the original and the written
  name, the source extents, the size, the confidence verdict, the timestamps, and
  a **SHA-256** of exactly the bytes that landed; per run, the source, the
  destination, the mode, the winner and suppressed counts, and the offsets a read
  stopped at. A failed artifact records no hash and no written name rather than a
  misleading one, and every name is escaped, so a filename holding a quote or a
  newline cannot break the document.
- `revenant::Sha256`: streaming SHA-256 (FIPS 180-4), pinned to the published
  vectors and fuzz-tested on the property that matters — hashing in one call and
  in two calls split anywhere must agree, which is exactly what a buffered hash
  gets wrong at the block and padding boundaries. The digest is taken as the
  bytes pass through the sink, so the manifest costs no second read of the
  recovered data.
- Content de-duplication, deferred from the `RecoverySink` story until there was
  a real hash to do it with: a carved artifact byte-identical to something
  already recovered is dropped in favour of it and counted. Only carved artifacts
  are ever dropped — two named files with identical content are two real files
  with two real names, and dropping either would be data loss dressed up as
  tidiness. Named artifacts are now written before carved ones so the anonymous
  copy always arrives second, while ordinals still come from device order, so the
  names on disk are exactly what they were.
- `--dry-run` on both frontends: the whole run except the last step. It scans,
  indexes, arbitrates and writes the manifest, and the destination is left
  exactly as it was found — so an operator can see what a recovery would produce
  before committing the disk space and the hours. Because ADR-0006 had already
  separated deciding from writing, a preview is not a mode the engine had to
  learn; it is the same run, one step shorter. The names it reports are the names
  a real run would use, collision renames and all, because it runs the same
  naming path rather than a second guess at it. It does not hash: a digest costs
  a full read of every artifact, which is most of what extraction is.
- Resumable scanning (ADR-0008): an interrupted recovery now costs the unscanned
  tail rather than the whole device — which matters because these runs take
  hours and the hardware is usually already failing, so re-reading a dying disk
  from zero can finish it off. The carve pass reports its progress after every
  bounded chunk of device, and the run answers back through the same seam: that
  one method is both how a checkpoint gets written and how `Ctrl-C` stops a scan
  cleanly instead of killing it. Re-running the same recovery into the same
  destination picks up where the last one stopped; a session belonging to a
  different run — different mode, formats, or device — is started fresh rather
  than half-matched, because the checkpoint identifies its run by a hash of the
  whole shape. The index is truncated back to the checkpoint on resume, so the
  tail an interrupted run appended past it does not double the candidates in the
  window the resumed scan is about to read again.
  **An interrupted run deliberately extracts nothing.** Arbitrating a partial
  index can crown a winner the finished scan would have suppressed — the
  candidate that beats it is in the tail nobody has read — so a stopped run
  writes no manifest, writes no files, exits non-zero, and says the scan is
  incomplete. What it leaves behind is exactly what the next run needs.
  Because discovery and extraction were already separate, "scan now, extract
  later" falls out of the same mechanism: a second run over a finished session
  has nothing left to scan and delivers from the index it already has.
- Seed corpora for the NTFS fuzz targets, generated reproducibly by
  `tools/fuzz/make_seed_corpus.py`. An empty corpus left the fuzz gate unable to
  reach past the `FILE`/`NTFS` magic within a short CI run.

### Changed
- `revenant-imagegen` now takes a subcommand: `pattern <output> <size> <name>`
  (the story-0007 behaviour) or `ntfs <output>`. The verb-less form is gone —
  a developer tool with no external consumers is better renamed than left with
  two silently overlapping grammars.
- `BootSector.cpp` split into the validation pipeline and `BootSectorFields.cpp`
  (per-field readers), keeping both well inside the file-length guard.
- C++ formatting convention: tab indentation (tabs for indent/continuation,
  spaces for alignment) and uniform parameter-list wrapping
  (`AlignAfterOpenBracket: AlwaysBreak` — no paren-column alignment);
  repo-wide mechanical reformat, recorded in `.git-blame-ignore-revs`.

### Fixed
- PNG and TIFF walk state was built with partial designated initializers, which
  MSVC accepts and clang rejects under `-Werror`
  (`-Wmissing-designated-field-initializers`). Caught by building the fuzz
  targets with clang locally; the Linux CI jobs would have gone red on it.
- Signature scanner: a magic sitting closer to the device start than its own
  in-file offset wrapped the unsigned `windowOffset + at - signature.offset`
  subtraction and invented a candidate near the end of the address space. Latent
  while every signature sat at offset 0; MP4's `ftyp` at offset 4 is the first
  that could reach it. The candidate start now goes through a checked helper,
  and a window travels with its device offset as one value.
- `sanitizeOutputPath` rejected every legitimate name when the output root was
  reached through a filesystem alias — a symlink or junction, or a Windows 8.3
  short name such as `C:\RECOVE~1`. Containment compared the assembled path,
  which keeps the caller's spelling, against a canonicalized root: two different
  namings of the same directory. Both sides are canonicalized now; the returned
  path still uses the caller's spelling. This is why the Windows CI job had been
  red since the guard landed — GitHub's temp directory arrives as `RUNNER~1`.
- NTFS MFT attribute walker: an end-marker attribute (`0xFFFFFFFF`) positioned
  below `usedSize` stopped parsing without advancing the walk offset, so the
  caller re-read the same marker for ever. A crafted record hung the parsing
  thread outright, wedging a device scan rather than crashing it. Found by the
  seeded fuzz corpus.
- NTFS MFT attribute walker: a first-attribute offset within four bytes of the
  record end produced an out-of-range read whose typed error was discarded by an
  unchecked `Result::value()`, turning it into an escaping `std::bad_variant_access`
  instead of an uncertain record. Attribute type and header are now read as
  bounds-checked steps.
- NTFS update-sequence fixup: an array shorter than the record's 512-byte stride
  count was accepted, leaving the uncovered strides holding their on-disk USN
  placeholder while the record still graded valid. The count must now cover every
  stride.
- NTFS resident attribute bounds check: content offset and length were summed in
  the attribute's own 32-bit width, so a hostile length could wrap past the check
  it should fail. The sum is now widened before comparison.

### Security
- `SECURITY.md` policy: threat model for parsing hostile bytes, path-traversal and
  bounded-allocation guarantees, and private vulnerability reporting.
- CI hardening: least-privilege workflow token (`permissions: contents: read`), pinned
  GitHub Action and npm tool versions (removed floating `@latest`); full SHA-pinning
  tracked as story-0008.
- Frozen contract files: `.claude/settings.json` denies assistant `Edit`/`Write` on
  `AGENTS.md`/`CLAUDE.md`, and a versioned pre-commit hook (`.githooks/pre-commit`)
  rejects any commit touching them (defense in depth across all edit paths).
- CI supply chain pinned to immutable identities: all GitHub Actions by commit
  SHA, vcpkg by commit, jscpd via committed npm lockfile (`npm ci`), and
  checkouts no longer persist credentials.

[Unreleased]: https://example.invalid/revenant/compare/v0.3.1...HEAD
[0.3.1]: https://example.invalid/revenant/compare/v0.3.0...v0.3.1
[0.3.0]: https://example.invalid/revenant/compare/v0.2.0...v0.3.0
[0.2.0]: https://example.invalid/revenant/compare/v0.1.0...v0.2.0
[0.1.0]: https://example.invalid/revenant/releases/tag/v0.1.0
