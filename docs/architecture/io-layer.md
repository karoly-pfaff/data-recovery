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
| `PhysicalDevice`   | A whole disk (`\\.\PhysicalDriveN`, `/dev/sdX`) | Needs elevated privileges. Read-only handle. |
| `VolumeDevice`     | A logical volume (`\\.\E:`, `/dev/sdX1`)  | Read-only; volume is not locked for write. |
| `NetworkBlockDevice` | A remote raw device (iSCSI target, NBD) | Future (M4+). Block-level, so full recovery works; behind the same interface. |
| `InMemoryDevice`   | A byte buffer                             | Test-only, in `tests/`.                 |

### Windows specifics

- Opened with `CreateFileW`, `GENERIC_READ`, `FILE_SHARE_READ | FILE_SHARE_WRITE`, no
  write access requested — the source can never be modified through our handle.
- Size discovery via `IOCTL_DISK_GET_LENGTH_INFO`; sector size via
  `IOCTL_STORAGE_QUERY_PROPERTY`.
- Physical-device reads must be sector-aligned; `PhysicalDevice` aligns internally and
  slices the requested sub-range from an aligned read.

### Linux specifics

- Opened with `open(path, O_RDONLY)`; `pread` for positioned reads (thread-safe, no
  shared file offset).
- Size via `BLKGETSIZE64`, sector size via `BLKSSZGET`, with a `lseek(SEEK_END)`
  fallback for image files.

The platform split lives behind the `PhysicalDevice`/`VolumeDevice` factory; the rest
of the codebase is platform-agnostic. Platform code is confined to `core/io/` and
selected with CMake target sources, not `#ifdef` sprinkled across the tree.

## Network and remote sources

Recovery requires **block-level** access to the source; this constraint defines what
"network source" means for Revenant ([ADR-0007](adr/adr-0007-block-level-access-boundary.md)):

- **Supported now — an image on a network share.** A `.dd`/`.img` on a UNC path or a
  mounted NFS/SMB path is opened by `ImageFileDevice` like any other file. Because it is
  a *raw image*, deleted-file and free-space recovery work fully. The only difference is
  I/O characteristics: higher latency and occasional transient failures, handled by the
  `CachingDevice` (coalesce into large reads) and `RetryingDevice` (retry with backoff,
  longer timeouts) decorators.
- **Supported later — a remote raw device.** `NetworkBlockDevice` (M4+) reads a device
  exposed over a block protocol (iSCSI, NBD) behind the same `BlockDevice` interface.
- **Not supported — a file-level network share.** Browsing `\\server\share` as a folder
  of files exposes only *live* files. There is no access to deleted entries, filesystem
  metadata, or unallocated space, so neither undelete nor carving is possible. Revenant
  will detect and clearly reject a file-level share used as a recovery source.

The destination may also be a network path. The CLI still enforces that the destination
differs from the source and warns if the destination is on unreliable/network storage,
since recovered data should not depend on the same flaky link.

## Decorators (composable, optional)

Implemented as `BlockDevice` wrappers so they compose and stay independently testable:

- **`CachingDevice`** — an LRU of fixed-size aligned blocks. Turns the many small,
  overlapping reads of parsing into few large device reads.
- **`RetryingDevice`** — on a read fault, retries with backoff, then falls back to
  returning zero-filled bytes for the unreadable sectors *and records the bad range*.
  Recovering from failing hardware means tolerating unreadable sectors, not aborting.

## Error model

`readAt` returns `Result<std::size_t>`. A short read at end-of-device is a value, not an
error. A hardware fault is a typed `IoError` carrying the offset and OS error code.
Bad-sector ranges are surfaced to the recovery layer so reports can note which regions
were unreadable.

## Testing

- `InMemoryDevice` backs the vast majority of unit tests — deterministic, fast, no
  privileges.
- `ImageFileDevice` is exercised against small synthetic images built by `tools/`.
- `CachingDevice`/`RetryingDevice` are tested against a fault-injecting fake that fails
  specified ranges.
