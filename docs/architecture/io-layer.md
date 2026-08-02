<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# I/O Layer — `BlockDevice`

The I/O layer isolates every "where do the bytes come from" concern behind one
interface, so that filesystems and the carving engine never know or care whether they
are reading a physical disk, a `.dd` image, or a mounted volume.

## The interface

```cpp
namespace revenant {

class BlockDevice {
public:
    virtual ~BlockDevice() = default;

    // Total addressable size in bytes.
    [[nodiscard]] virtual std::uint64_t sizeInBytes() const = 0;

    // Native sector size (512 or 4096). Reads need not be sector-aligned;
    // the device handles alignment internally.
    [[nodiscard]] virtual std::uint32_t sectorSize() const = 0;

    // Read exactly `buffer.size()` bytes starting at `offset`. Returns the
    // number of bytes read, or a typed error on a hardware/read fault.
    [[nodiscard]] virtual Result<std::size_t>
    readAt(std::uint64_t offset, std::span<std::byte> buffer) = 0;
};

} // namespace revenant
```

The interface is deliberately tiny: **random-access, read-only, byte-addressed**.
Everything else (caching, retry, alignment) is an implementation detail or a decorator.

## Concrete implementations

| Class              | Source                                    | Platform notes                          |
|--------------------|-------------------------------------------|-----------------------------------------|
| `ImageFileDevice`  | A raw image file (`.dd`, `.img`) — local **or** on a network share (UNC `\\server\share\disk.img`, or a mounted NFS/SMB path) | Portable. The default for development. Network paths just work; latency is absorbed by the caching/retry decorators. |
| `RawDevice`        | A whole disk (`\\.\PhysicalDriveN`, `/dev/sdX`) **or** a volume (`\\.\E:`, `/dev/sdX1`) | Needs elevated privileges. Read-only handle; the volume is not locked for write. |
| `NetworkBlockDevice` | A remote raw device (iSCSI target, NBD) | Future (M6). Block-level, so full recovery works; behind the same interface. |
| `InMemoryDevice`   | A byte buffer                             | Test-only, in `tests/`.                 |

**One class for disks and volumes** (story-0401). The layer once named a
`PhysicalDevice` and a `VolumeDevice`; they turned out to be the same object. On both
platforms a disk and a volume are opened by the same call and measured by the same
query, and differ only in the path an operator types — which is exactly what this layer
exists to hide. Two classes would have been one implementation copied.

### Choosing the device

`openSource(path)` is the single factory: a path that is a **regular file** opens as an
`ImageFileDevice`, and anything else as a `RawDevice`. That question is the right one
rather than a convenient one — a whole disk and a mounted volume are precisely the
things a filesystem reports as *not* regular files, on both platforms — so no layer has
to learn how a device path is spelled.

### Windows specifics

- Opened with `CreateFileW`, `GENERIC_READ`, `FILE_SHARE_READ | FILE_SHARE_WRITE`, no
  write access requested — the source can never be modified through our handle.
  `FILE_SHARE_WRITE` is what lets a disk Windows itself has open be read at all.
- Size via `IOCTL_DISK_GET_LENGTH_INFO`; sector size via
  `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX`, falling back to 512 when the device will not say.
- `ERROR_ACCESS_DENIED` becomes `kPermissionDenied`, so the CLI can say *run it
  elevated* rather than send the operator looking for a missing path.

### Linux specifics

- Opened with `open(path, O_RDONLY)`; `pread` for positioned reads (thread-safe, no
  shared file offset).
- Size via `BLKGETSIZE64`, sector size via `BLKSSZGET`, with a `lseek(SEEK_END)`
  fallback for image files. `EACCES`/`EPERM` become `kPermissionDenied`.

### Alignment

A raw device accepts only reads whose offset *and* length are whole sectors.
`BlockDevice` promises the opposite — an MFT record is 1024 bytes at an arbitrary
offset, a directory entry is 32 — and every parser in the tree takes that promise. So
`RawDevice` reconciles the two internally: `AlignedRead.hpp` widens a request to the
enclosing whole sectors, reads that, and slices the caller's bytes back out. A request
that is *already* aligned goes straight through to the caller's own buffer, so a cache
or a scan window above it costs no copy.

That logic is a platform-neutral header rather than a method precisely so it can be
tested without a disk: `readThroughAlignment` takes the platform's read as a callable,
and the tests supply one that refuses unaligned requests exactly as the hardware does.

The platform split lives behind `RawDevice`; the rest of the codebase is
platform-agnostic. Platform code is confined to `core/io/` and selected with CMake
target sources, not `#ifdef` sprinkled across the tree.

## Network and remote sources

Recovery requires **block-level** access to the source; this constraint defines what
"network source" means for Revenant ([ADR-0007](adr/adr-0007-block-level-access-boundary.md)):

- **Supported now — an image on a network share.** A `.dd`/`.img` on a UNC path or a
  mounted NFS/SMB path is opened by `ImageFileDevice` like any other file. Because it is
  a *raw image*, deleted-file and free-space recovery work fully. The only difference is
  I/O characteristics: higher latency and occasional transient failures, handled by the
  `CachingDevice` (coalesce into large reads) and `RetryingDevice` (retry with backoff,
  longer timeouts) decorators.
- **Supported later — a remote raw device.** `NetworkBlockDevice` (M6) reads a device
  exposed over a block protocol (iSCSI, NBD) behind the same `BlockDevice` interface.
- **Not supported — a file-level network share.** Browsing `\\server\share` as a folder
  of files exposes only *live* files. There is no access to deleted entries, filesystem
  metadata, or unallocated space, so neither undelete nor carving is possible. Revenant
  will detect and clearly reject a file-level share used as a recovery source.

The destination may also be a network path. The CLI still enforces that the destination
differs from the source and warns if the destination is on unreliable/network storage,
since recovered data should not depend on the same flaky link.

## Decorators, and the stack every run composes

Implemented as `BlockDevice` wrappers so they compose and stay independently testable:

- **`CachingDevice`** — an LRU of fixed-size aligned blocks. Turns the many small,
  overlapping reads of parsing into few large device reads.
- **`RetryingDevice`** — on a read fault, retries with backoff, then falls back to
  returning zero-filled bytes for the unreadable sectors *and records the bad range*.
  Recovering from failing hardware means tolerating unreadable sectors, not aborting.

`openSource` returns a **`SourceStack`**: the concrete device with `RetryingDevice` over
it, owned together. There is no bare-device path and no flag for one — an image on a
network share wants the same treatment as a failing disk.

A run reads the same sector more than once — once to scan it, once to extract from it —
and nothing between the run and the device remembers the first read. So `badRanges()` is
a **set** of ranges rather than a log of the reads that met them: recording each
encounter would report twice the damage there is and grow without bound on a failing
drive, which is what [ADR-0009](adr/adr-0009-output-safety.md) forbids.

**`CachingDevice` is not in that stack, and the reason is a measurement.** story-0604
first composed it above the retry layer, reasoning that the block it keeps spares a dying
drive the repeat reads. The benchmark gate then measured what it costs a healthy one: on
`carve-validate` the composed cache was 42% slower than the bare device
(3,070 -> 1,767 candidates/s on the Linux workbench) and CI counted fifty times the
instructions, because a scan reads forward in large strides and a 64 KiB block cache
turns each of those into an allocation and a second copy. Its other claim — that it makes
every read sector-aligned for a Windows raw device — is redundant: `RawDevice::readAt`
aligns its own reads through `readThroughAlignment`. The decorator and its tests stand;
nothing composes it. What would put it back is a number from the access pattern it was
built for, on a device where read *latency* dominates rather than an image file on local
storage.

The stack also owns `badRanges()`, and deliberately: a `RetryingDevice` knows what it
invented, but a `PartitionView` over one does not and would report a clean device while
sitting on top of damage. Widening `BlockDevice` with the question would make every
implementation answer something only one of them can, and make the answer depend on which
layer of a composition the caller happens to hold ([ADR-0007](adr/adr-0007-block-level-access-boundary.md)).
The map belongs to the thing that did the composing.

Until story-0604 this section described a composition no shipped binary performed:
`openSource` built a bare device, and the decorators had no production consumer at all.
One of the two still has none — but that is now stated here rather than implied.

## Error model

`readAt` returns `Result<std::size_t>`. A short read at end-of-device is a value, not an
error. A hardware fault is a typed `IoError` carrying the offset and OS error code.
Bad-sector ranges are surfaced to the recovery layer so reports can note which regions
were unreadable.

## Acquiring the source: image first

For **failing hardware**, the recommended workflow is to acquire a full image once and
run recovery against the image, rather than repeatedly reading a dying disk:

- Every extra read of a failing drive risks accelerating its death. A single, forward-only
  imaging pass (ddrescue-style, with a bad-sector map) minimizes stress on the source.
- Working from an image is faster (no per-read device latency), reproducible, and safe to
  re-run — which pairs naturally with [resumable recovery](adr/adr-0008-resumability-checkpointing.md).

Revenant supports this today by recovering from any image via `ImageFileDevice`. A
dedicated **imaging mode** — a forward-only, bad-sector-tolerant acquisition that emits an
image plus a bad-sector map (consumable by the `RetryingDevice`) — is planned for M6. The
documentation and CLI guidance recommend imaging-first for failing media regardless.

## Testing

- `InMemoryDevice` backs the vast majority of unit tests — deterministic, fast, no
  privileges.
- `ImageFileDevice` is exercised against small synthetic images built by `tools/`.
- `CachingDevice`/`RetryingDevice` are tested against a fault-injecting fake that fails
  specified ranges.
