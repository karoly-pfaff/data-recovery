<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0609: A destination on the source disk is refused before the run starts

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: Ready
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
  before starting" (lines 30–31). Read-only-by-construction guards the source
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

- [ ] A run whose source is a raw device and whose destination lies on it — a
      destination volume with an extent on a whole-disk source, or a
      destination on a volume source itself — exits nonzero before the first
      read of the source, with nothing created in the destination.
- [ ] The refusal prints its own sentence naming the conflict and the move to
      different physical storage; `kInvalidArgument`'s sentence no longer
      claims the destination rule.
- [ ] A destination on storage the source domain does not touch is accepted —
      including, for a volume source, a sibling volume on the same disk. The
      allowance is tested, not incidental.
- [ ] An image-file source behaves exactly as today: the three refusals at
      `RecoverySinkTest.cpp:150-173` pass unchanged, and device identity is
      never consulted.
- [ ] The overlap decision is unit-tested over injected identities, both
      verdicts, on both platforms in CI — including a multi-extent destination
      and the no-local-extent (network) destination.
- [ ] For a device source, an identity-resolution failure refuses the run with
      the OS code attached.
- [ ] The loop-device leg has run green on the workbench and its transcript is
      recorded in this story.
- [ ] ADR-0005:30-31 is true as written;
      [recovery-output.md](../../architecture/recovery-output.md):63-64 and the
      `RecoverySink.hpp:60-62` comment describe the two-tier rule;
      `CHANGELOG.md` updated under `[Unreleased]`.

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

- [ ] Acceptance criteria met, tests green under ASan + UBSan.
- [ ] clang-format, clang-tidy, duplication and file-length guard clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
