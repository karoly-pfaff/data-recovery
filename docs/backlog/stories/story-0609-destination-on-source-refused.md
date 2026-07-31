<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0609: A destination on the source disk is refused before the run starts

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: In review
- Size: M

## Goal

Make ADR-0005's promise true for the sources that need it most: a destination
directory that lives on the device being recovered is refused before the first
read, by physical identity rather than path spelling. Today the only guard is a
lexical prefix check written when every source was an image file;
`\\.\PhysicalDrive0` never path-prefixes `C:\recovered`, so recovery output can
land on the very unallocated clusters it is being recovered from — the one loss
mode a read-only recovery tool exists to prevent, delivered by the tool itself.

## Design references

- [ADR-0005](../../architecture/adr/adr-0005-read-only-by-default.md) — the
  promise this story keeps: "on a different volume; the CLI validates this
  before starting", under its **Validated** heading — story-0614 split the record
  into its by-construction and validated halves and moved that sentence's line. Read-only-by-construction guards the source
  *handle*; this is the other half, guarding the source from the destination.
- [ADR-0007](../../architecture/adr/adr-0007-block-level-access-boundary.md) —
  "the CLI still enforces destination ≠ source" (lines 43–44), and the network
  destination the same lines permit, which this story must not break.
- [story-0109](story-0109-recovery-sink.md) — where the lexical check came
  from, in a milestone whose every source was a file.
- [story-0406](story-0406-reject-file-shares.md) — the refusal standard this
  story follows: a code of its own when the action it calls for is different,
  and a sentence that names the fix, not the failure.
- [story-0603](story-0603-linux-loop-device.md) — the workbench mold for the
  leg no CI runner can host, and the loop device this story borrows.
- [`RecoverySink.cpp:34-41`](../../../src/recovery/RecoverySink.cpp) /
  [`RecoverySink.hpp:60-64`](../../../include/revenant/recovery/RecoverySink.hpp)
  — the check as it stands, and the contract comment over it.
- [`RecoveryRun.cpp:179-188`](../../../src/cli/RecoveryRun.cpp),
  [`SourceDevice.cpp:49-57`](../../../src/core/io/SourceDevice.cpp) — the seam:
  the source opens, then the sink validates against the raw path string.
- [`RawDevice.hpp:27-28`](../../../include/revenant/core/io/RawDevice.hpp) —
  the platform-pair pattern (`NativeIoWindows/Posix`,
  `RawDeviceWindows/Posix`) the new identity code follows.

## What was measured

Confirmed twice by the M5 audit
([epic-m5](../epic-m5-performance.md#milestone-architecture-audit), lines
144–148) and re-verified against the current tree:

- The project's only destination-vs-source guard is `contains()` at
  `src/recovery/RecoverySink.cpp:34-41`, applied at `:72` inside
  `RecoverySink::open`: both paths through `weakly_canonical`, then
  `std::ranges::mismatch` — an element-wise prefix comparison of *path
  spellings*. It answers "is the source inside the destination directory",
  which was the whole question when story-0109 wrote it and every source was
  an image file.
- M4 made raw devices first-class and routed them through that gate unchanged.
  `runRecovery` (`src/cli/RecoveryRun.cpp:179-188`) opens the source —
  `openSource` (`src/core/io/SourceDevice.cpp:49-57`) sends anything that is
  neither directory nor regular file to `RawDevice::open`, which is
  `\\.\PhysicalDrive0`, `\\.\C:` and `/dev/sda` — then hands the raw path
  string to `RecoverySink::open`. `\\.\PhysicalDrive0` shares no path element
  with `C:\recovered`, so validation passes and every artifact the run writes
  lands on unallocated clusters of the disk being recovered.
- No volume-identity mechanism exists anywhere in the tree: a grep for
  `GetVolumePathName`, `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`,
  `GetVolumeInformation`, `st_dev`, `st_rdev` finds nothing — not in `src/`,
  not in `include/`, not in tests.
- The refusal's only test is the lexical image case:
  `RecoverySink.RefusesADestinationHoldingTheSource`
  (`tests/unit/recovery/RecoverySinkTest.cpp:168-173`), a `TempDir` holding
  `disk.img`.
- The prose is ahead of the code in three places: ADR-0005:30-31 ("the CLI
  validates this before starting"),
  [recovery-output.md:63-64](../../architecture/recovery-output.md)
  ("Destination ≠ source, on different storage. **Enforced**"), and the
  contract comment at `RecoverySink.hpp:60-62`.
- The sentence dates itself: `describe(kInvalidArgument)`
  (`src/cli/RunSummary.cpp:96-97`) says the destination must "not contain the
  source" — a rule about spelling — and the same code also serves a name
  nothing safe survived (`RecoverySink.cpp:95`), so one sentence covers two
  unrelated failures.

## Design decisions

**Identity, not spelling.** The destination directory is resolved to the
physical storage it occupies and compared against the domain the source reads;
overlap refuses the run before discovery starts. Windows: `GetVolumePathName`
on the destination names its volume, and
`IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` on that volume yields its
{disk number, offset, length} extents — one for a plain partition, several for
a spanned volume, so the set is the identity and spanned storage is covered
for free. A whole-disk source's domain is its disk number
(`IOCTL_STORAGE_GET_DEVICE_NUMBER`); a volume source's domain is its own
extents; refuse when a destination extent lies inside the source domain.
POSIX: `stat().st_dev` of the destination against the source node's
`st_rdev` — equal means the destination's filesystem is mounted from the very
device being read — and for a whole-disk source the destination's device is
resolved to its owning disk through `/sys/dev/block/<major>:<minor>`, the
kernel's own partition-to-disk answer.

**What is refused, exactly — and what is deliberately allowed.** Whole-disk
source: a destination whose volume has any extent on that disk is refused.
Volume source: a destination on that volume is refused. A destination on a
*sibling* volume of the same disk as a volume source is allowed: the loss mode
is overwriting the clusters under recovery, and a sibling volume holds none of
them. Whether writing anything next door to a dying disk is wise is the
operator's judgment; overwriting the bytes being recovered is not, and the
line between the two is drawn here on purpose.

**Mechanism in `core/io`, policy in the sink.** The resolution is Win32 and
sysfs, so it lives as a platform pair beside the code that already splits this
way (`RawDevice.hpp:27-28`) — `DeviceIdentityWindows.cpp` /
`DeviceIdentityPosix.cpp` behind one platform-neutral header carrying the
identity types and a pure `overlaps(source, destination)`. Identities are
computed from the two paths, with zero-access query handles on Windows (an
identity question needs no read grant) and `stat` on POSIX: `BlockDevice`
stays the tiny read seam story-0604 defends, `openSource` does not change, and
this story is indifferent to
[story-0604](story-0604-bad-sector-map-to-manifest.md)'s stack rework — the
two can land in either order. The policy call sits in `RecoverySink::open`,
whose documented contract is already "validates the destination once"
(`RecoverySink.hpp:60-62`): story-0406 refused a bad source where sources are
opened, and this refuses a bad destination where destinations are validated.

**The lexical check stays, for the case it was right about.** An image-file
source keeps today's rule — the run must not grow its output tree around the
image it is reading — and never consults device identity: a destination
sharing a volume with a disk image is normal practice, not a loss mode. The
classification deciding which rule applies is the one `openSource` already has
(`SourceDevice.cpp:49-57`), exposed rather than duplicated.

**A code of its own, and a sentence that names the next step.**
`Error.hpp:8` extends only when a story needs one, and this story needs one by
story-0406's own test: the action the operator must take — move the output to
different physical storage — is not `kInvalidArgument`'s. `kDestinationOnSource`,
rendered by `describe` as a sentence naming the conflict (the destination sits
on the device being recovered, and recovery would overwrite the clusters it
reads) and the fix. `kInvalidArgument` goes back to serving the name-collision
failure alone.

**When identity cannot be answered, the run does not gamble.** For a device
source, a destination whose identity resolution fails is refused with the OS
code attached — when the check cannot prove the destination is elsewhere,
"elsewhere" is not assumed. ADR-0007's permitted network destination never
hits this: a share resolves cleanly to no local extents on Windows and to a
`dev_t` no block device owns on POSIX, which overlaps nothing. Failing closed
covers the exotic, not the networked.

## Acceptance criteria

- [x] A run whose source is a raw device and whose destination lies on it — a
      destination volume with an extent on a whole-disk source, or a
      destination on a volume source itself — exits nonzero before the first
      read of the source, with nothing created in the destination.
- [x] The refusal prints its own sentence naming the conflict and the move to
      different physical storage; `kInvalidArgument`'s sentence no longer
      claims the destination rule.
- [x] A destination on storage the source domain does not touch is accepted —
      including, for a volume source, a sibling volume on the same disk. The
      allowance is tested, not incidental.
- [x] An image-file source behaves exactly as today: the three refusals at
      `RecoverySinkTest.cpp:150-173` pass unchanged, and device identity is
      never consulted.
- [x] The overlap decision is unit-tested over injected identities, both
      verdicts, on both platforms in CI — including a multi-extent destination
      and the no-local-extent (network) destination.
- [x] For a device source, an identity-resolution failure refuses the run with
      the OS code attached.
- [x] The loop-device leg has run green on the workbench and its transcript is
      recorded in this story.
- [x] ADR-0005:30-31 is true as written;
      [recovery-output.md](../../architecture/recovery-output.md):63-64 and the
      `RecoverySink.hpp:60-62` comment describe the two-tier rule;
      `CHANGELOG.md` updated under `[Unreleased]`.

## What it turned out to be

**Both sides resolve to the same shape, so the decision is one function.** A
Windows destination is a set of `{disk number, offset, length}` extents from
`IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`; a POSIX one is a partition's `start`
and `size` from sysfs against its parent disk's `dev_t`. Written as
`StorageExtent{disk, offsetBytes, lengthBytes}` both platforms answer the same
question, and `overlaps()` is a pure byte-range intersection over two sets —
which is why the sibling-volume allowance, the spanned destination and the
network destination all fall out of one rule rather than needing three.

**A whole disk is stated as `kWholeDisk`, not measured.** Every byte of the
disk, so every volume on it is inside. Asking the OS for the disk's size would
add a query that can fail to a question that does not need one, and the overlap
test compares by the distance between two starts rather than by computing each
range's end, so a length of `UINT64_MAX` cannot wrap the arithmetic into
reporting no overlap.

**The lexical rule now runs first, for every source.** The story planned to
choose one tier or the other by classification. That breaks the refusal
`RecoverySinkTest.cpp:168-173` asserts, because the source it names does not
exist and a path that names nothing classifies as a *device* — deliberately, so
`openSource` lets the OS give its own reason (`SourceDevice.hpp:26-29`). Running
the spelling rule first and the identity rule only after it passes keeps that
refusal exactly as it was, and costs nothing: against a real device the spelling
rule never fires, because a raw device path lies under no directory.

**No probe seam was needed.** `refuseOverlap` takes the two resolved
`Result<StorageExtents>` values, so every verdict — both outcomes, both
resolution failures — is driven by identities handed straight in. The resolvers
are then one line of wiring above it, and that line is what the workbench leg
below covers.

**`describe` had to be split.** One more `ErrorCode` took it past the
statement threshold. It is now `beforeTheRun` / `duringTheRun`: the failures
that stop a run before it starts and are fixed by changing an argument, and the
ones that happen while it runs. Both switches list the enum exhaustively, so
adding a code is still a compile error until it is given a sentence.

**`queryDevice` is shared.** `RawDeviceWindows.cpp` already had the
`DeviceIoControl`-into-a-struct helper the identity resolution needs. Copying it
would have been a DRY-gate failure, so it moved to
`src/core/io/WindowsDeviceQuery.hpp`, carrying the handle as the `std::intptr_t`
the I/O layer already passes native handles in — which keeps `windows.h` out of
the header.

## Workbench transcript (Debian WSL2, 2026-07-31)

`/dev/loop0` over a 256 MiB two-partition image, both partitions ext4, mounted
at `/mnt/rec1` and `/mnt/rec2`. Resolution first — the destination-side and
source-side resolvers agreeing on one volume is the crux of the whole story:

```
storageOf(/dev/loop0)        -> disk=1792 offset=0 length=18446744073709551615
storageOf(/dev/loop0p1)      -> disk=1792 offset=1048576 length=104857600
storageUnder(/mnt/rec1)      -> disk=1792 offset=1048576 length=104857600
```

`1048576 = 2048 × 512` and `104857600 = 204800 × 512`, which is what
`/sys/block/loop0/loop0p1/{start,size}` says. Then the four runs, through the
shipped `revenant-undelete`:

```
--- whole disk -> destination on one of its volumes
    [error] the destination is on the storage being recovered, or could not be
    shown to be elsewhere; writing there would overwrite the very clusters the
    run reads. Point --destination at a different physical disk
    exit=1
    destination entries: 0
--- volume -> destination on that same volume
    [error] (the same sentence)
    exit=1
    destination entries: 0
--- volume -> destination on a sibling volume of the same disk (allowed)
    exit=0
--- positive control: volume -> destination on the distro's own disk
    exit=0
```

The Windows resolvers were driven the same way against this machine's real
disks, unelevated — the identity query opens with zero desired access, so it
needs no privilege:

```
storageOf(\\.\PhysicalDrive0) -> disk=0 offset=0 length=18446744073709551615
storageUnder(C:\Users)        -> disk=0 offset=290455552 length=1021821583360
storageUnder(D:\Projects)     -> disk=1 offset=16777216 length=53687091200

\\.\PhysicalDrive0 -> C:\Users    : REFUSED (on the source)
\\.\PhysicalDrive0 -> D:\Projects : ALLOWED
\\.\C:             -> C:\Users    : REFUSED (on the source)
\\.\C:             -> D:\Projects : ALLOWED
```

`\\.\PhysicalDrive0 -> C:\Users` is the exact run this story exists to stop.

Then the elevated run itself, which is where the frontend first reaches the
check on Windows at all — unelevated, `openSource` refuses a physical drive
first with `kPermissionDenied`:

```
=== whole disk 0 (holds C:) -> destination on C: : must be REFUSED
[error] the destination is on the storage being recovered, or could not be
shown to be elsewhere; writing there would overwrite the very clusters the run
reads. Point --destination at a different physical disk
    exit=1
```

The refused destination held **0 entries** afterwards. The control — the same
whole-disk source with its destination on D:, a different disk — was *not*
refused: it opened the disk and began scanning, writing its session directory
(`.revenant/`, `candidates.dat`, `candidates.idx`, `checkpoint`) into the
destination, and was stopped by hand after seven minutes of CPU rather than
left to carve a terabyte. Passing the destination check is the whole of what
that control had to show.

## Test plan

Unit (`tests/unit/io/DeviceIdentityTest.cpp`): the decision function over
injected identities — whole-disk source against a destination with an extent
on that disk (refused), all extents elsewhere (allowed), a spanned destination
with one extent of several on the source disk (refused); volume source against
the same volume (refused) and a sibling volume on the same disk (allowed); a
destination with no local extents (allowed). Resolution smoke over paths CI
can reach: the scratch directory resolves to a volume with at least one extent
(Windows) or a nonzero device (POSIX), and a regular file classifies as not a
device.

Unit (`tests/unit/recovery/`, `tests/unit/cli/`): today's three
`RecoverySink::open` refusals unchanged; the sink's device branch driven
through the same injected-identity seam the decision tests use — the seam's
exact shape is the implementer's, its testability is not; `RunSummaryTest`
asserts the new sentence names the conflict and the step, per story-0406's
plan.

Workbench (story-0603 mold, `tools/loopdev/`): attach the fixture disk with
`losetup -P`, mount its ext4 partition, put a destination directory on that
mount. Then: `--source /dev/loopN` into that destination is refused — nonzero
exit, the sentence, an empty destination; `--source /dev/loopNp4` into it is
refused (destination `st_dev` equals the node's `st_rdev` — the identity
itself, observable); `--source /dev/loopNp1` into it runs to completion (the
sibling-volume allowance, proven); and a destination on the distro's own disk
recovers normally as the positive control. Transcript recorded in this story
on completion.

Manual, recorded (Windows): one elevated run with `--source
\\.\PhysicalDrive<n>` and a destination on any volume of that disk, refused
before any read — safe to demonstrate precisely because the check fires before
the run and the source handle is read-only by construction (ADR-0005).

Not automated: a real physical-disk refusal in CI — runners hand out no raw
disks. The decision function and destination-side resolution are the
CI-testable surface; the line is drawn where story-0603 drew it.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan (1033/1033).
- [x] clang-format, clang-tidy, duplication and file-length guard clean.
      `tidy` reports nothing against any file in this diff; the `tests/fuzz/*`
      errors it does report are a pre-existing local artifact of the Windows
      clang build directory carrying no compile commands for those TUs, and
      touch no file this story changes.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
