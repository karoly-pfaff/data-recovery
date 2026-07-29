<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0301: The `fs::FileSystem` seam — one interface, one way in

- Epic: [epic-m3-filesystem-breadth](../epic-m3-filesystem-breadth.md)
- Status: Done
- Size: M

## Goal

Give the three filesystems M3 adds one interface to arrive behind, and one factory
that decides which of them a volume carries — so FAT32, exFAT and ext4 are each a
parser plus one table entry, not three rewrites of the same wiring.

## Why this is a story and not a footnote

M1 shipped without the seam on purpose: one implementation does not justify an
abstraction, and inventing it before there was anything to vary it against would
have been guesswork ([filesystems.md](../../architecture/filesystems.md)). What
exists today is `recovery::enumerateVolume`, a free function that reads a boot
sector, opens an `MftTable` and walks it — NTFS wired straight into the recovery
layer. Every M3 story needs that wiring undone, and undoing it once is the whole
of this story.

It is small, it touches `HybridRecovery`, and it changes one observable error
code. Doing it inside story-0302 would bury a cross-cutting refactor inside a
FAT32 parser and make both harder to review.

## Design references

- [Filesystem layer](../../architecture/filesystems.md) — the shared vocabulary
  (`RecoveredEntry`, `EntryVisitor`) this seam completes, and the promise that the
  interface arrives with the second filesystem.
- [ADR-0006](../../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md)
  — enumeration reports to a visitor and never extracts. The seam inherits that:
  a `FileSystem` has no method that returns bytes.
- [ADR-0005](../../architecture/adr/adr-0005-read-only-by-default.md) — mounting
  reads; there is no write path to leave out.
- [Hybrid orchestration](../../architecture/hybrid-orchestration.md) — a volume
  that will not mount downgrades a hybrid run to carving rather than failing it.

## Scope

1. **The interface** — `include/revenant/fs/FileSystem.hpp`: `fs::EnumerationStats`
   (promoted out of `fs::ntfs`, since every filesystem reports it) and
   `fs::FileSystem`, an already-mounted volume with one method, `enumerate`.
2. **The factory** — `include/revenant/fs/Mount.hpp` / `src/fs/Mount.cpp`:
   `mountVolume(BlockDevice&)` offers the volume to each filesystem this build can
   read, in order, and hands back the first that recognizes it.
3. **NTFS behind it** — `src/fs/ntfs/NtfsFileSystem.{hpp,cpp}`: the `MftTable` +
   `enumerateEntries` pair, mounted once and walked on demand. `mountNtfs` is the
   table's only NTFS entry.
4. **The recovery layer off NTFS** — `recovery::enumerateVolume` keeps its
   signature and its job ("mount one volume and walk it") but stops naming a
   filesystem; nothing under `src/recovery/` includes an `ntfs` header any more.

## Design decisions

**Mounting is a separate step from walking.** Recognizing a volume is a parse that
either succeeds or fails; walking it is a traversal that reports as it goes. Fusing
them would mean re-parsing the geometry on every walk and would give the interface
a method that does two things. `FileSystem` is therefore what a *successful mount*
returns, and it has exactly one method.

**"Not my filesystem" is `kNotFound`; everything else is an answer.** Each mounter
checks its own signature first — NTFS's OEM id, FAT32's `FAT32` type string, exFAT's
boot name, ext4's superblock magic. A volume that does not carry that signature is
declined with `kNotFound` and the next filesystem is asked. A volume that *does*
carry it belongs to that mounter, and its parse failure is returned unchanged rather
than passed over. A corrupt NTFS volume is not an unknown volume, and reporting it as
one would send the run hunting for a FAT that was never there.

**The probe table is ordered and fixed at build time.** A registry that filesystems
register into at runtime would be speculative generality (YAGNI): the set is known
when the binary is linked, exactly like `builtinCarvers`. Order matters and is
stated in one place — an exFAT volume also carries a FAT-shaped BPB, so its mounter
must be asked before FAT32's.

**A volume no filesystem recognizes is `kNotFound`, not `kInvalidArgument`.** This
is the one observable change. Today a zeroed boot sector fails NTFS's OEM check and
surfaces as `kInvalidArgument` — "this NTFS volume is broken". With four filesystems
that is simply wrong: nothing recognized it, which is a different fact and the one a
formatted or RAW volume actually presents. `HybridRecovery` treats every mount
failure alike (downgrade in hybrid mode, fail in filesystem-only mode), so behaviour
does not change — only the code the failure carries, and the test that asserts it.

**Timestamps stay NTFS FILETIME ticks.** The shared vocabulary needs one epoch, and
this is the one already in `fs::Timestamps` and the manifest. It is also the widest
and finest of the four: FAT's DOS time (2 s, 1980–2107) and ext4's 32-bit Unix
seconds both convert into it without loss, and the reverse would not hold. Each new
filesystem converts on the way out; the conversion helpers arrive with the first
filesystem that needs one, not here.

**`FileSystem` does not report its own name.** It would be one line, and nothing
consumes it: `RecoveryStats` says whether a filesystem mounted, not which. Naming
the mounted filesystem in the run summary and the manifest is a real improvement and
a real story, with its own plumbing through `RecoveryStats`, `RunSummary` and the
manifest schema — it is not a free rider on this one.

## Acceptance criteria

### The interface

- [x] `include/revenant/fs/FileSystem.hpp` exposes `fs::EnumerationStats`
      (`recordsScanned`, `entriesReported`) and the abstract `fs::FileSystem` with
      one method: `Result<EnumerationStats> enumerate(EntryVisitor&) const`.
- [x] `fs::ntfs::EnumerationStats` is gone; `enumerateEntries` returns the shared
      `fs::EnumerationStats`.
- [x] `FileSystem` is non-copyable and non-movable, and owns no device — the
      borrowed `BlockDevice` must outlive it, as everywhere else in the layer.

### The factory

- [x] `fs::mountVolume(BlockDevice&)` returns `Result<std::unique_ptr<FileSystem>>`.
- [x] A volume carrying a valid NTFS boot sector mounts, and the mounted filesystem
      enumerates exactly the entries `enumerateEntries` reports for it.
- [x] A volume no mounter recognizes is `kNotFound`.
- [x] A volume whose NTFS signature is intact but whose geometry is not fails with
      the parser's own error (`kInvalidArgument`), not `kNotFound` — recognized and
      broken is not the same as unrecognized.
- [x] A device too short to hold a boot sector is `kNotFound`, not a truncated read.
- [x] A device read fault surfaces as `kIoFailure`.

### The recovery layer

- [x] `recovery::enumerateVolume` returns `Result<fs::EnumerationStats>` and reaches
      NTFS only through `fs::mountVolume`.
- [x] Nothing under `src/recovery/` includes a header from `revenant/fs/ntfs/`.
- [x] A hybrid run over an unmountable volume still carves, and a filesystem-only
      run over one still fails — now with `kNotFound`.

## Test plan

Unit (`tests/unit/fs/MountTest.cpp`): the fixture volume mounts and enumerates the
same four entries the direct walk reports; a zeroed boot sector is `kNotFound`; an
NTFS OEM id over a corrupt BPB is `kInvalidArgument`; a device shorter than a boot
sector is `kNotFound`; a device that faults on read is `kIoFailure`.

Unit (`tests/unit/fs/ntfs/EntryEnumerationTest.cpp`): unchanged except for the stats
type's namespace — the walk itself is not what this story changes.

Unit (`tests/unit/recovery/HybridRecoveryTest.cpp`): the filesystem-only run over an
unmountable volume now asserts `kNotFound`; every other expectation is unchanged,
which is the point.

Fuzz: **no new target, deliberately.** The seam adds no byte parser — `recognize`
is `oemIdIsValid` followed by `parseBootSector`, both of which `NtfsBootSectorFuzz`
already drives with arbitrary bytes. Retargeting `NtfsEnumerateFuzz` at
`mountVolume` was considered and rejected: it mounts its input against a fixed
geometry precisely so the fuzzer spends its budget inside the `$MFT` walk, and
making every mutation first preserve a valid boot sector would shrink the walk's
reachable state space for no new coverage (the same argument
`tools/fuzz/make_seed_corpus.py` opens with). The first *new* parser this epic
adds — FAT32's — brings its own target.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Coverage held or raised (>= 85% core) — enforced by the CI coverage job;
      the instrumentation is GCC/Clang-only, so it is not reproducible on the
      local MSVC build.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked; `docs/architecture/filesystems.md` and `docs/roadmap.md` no
      longer describe the seam as already existing.
