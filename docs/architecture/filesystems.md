<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Filesystem Layer

The filesystem layer powers `revenant-undelete`: it reads on-disk metadata to recover
deleted files **with their original names, paths, and timestamps** — the information
carving alone can never reconstruct. All parsers are strictly **read-only**.

## The interface

```cpp
namespace revenant::fs {

struct RecoveredEntry {
    std::filesystem::path path;      // reconstructed path within the volume
    std::uint64_t sizeInBytes;
    std::vector<Extent> extents;     // where the data lives on the device
    Timestamps timestamps;           // created / modified / accessed
    EntryState state;                // Live, Deleted, Orphaned
    Confidence recoverability;       // how intact the metadata is
};

class FileSystem {
public:
    virtual ~FileSystem() = default;

    // Enumerate entries, including deleted and orphaned ones.
    [[nodiscard]] virtual Result<void>
    enumerate(EntryVisitor& visitor) = 0;

    // Read an entry's data extents through the underlying BlockDevice.
    [[nodiscard]] virtual Result<std::size_t>
    readEntry(const RecoveredEntry& entry, std::uint64_t offset,
              std::span<std::byte> buffer) = 0;
};

} // namespace revenant::fs
```

A `FileSystem` is constructed over a `BlockDevice` and a byte range (a partition located
by the [volume layer](overview.md)). It never writes.

## Supported filesystems

| Filesystem | Deleted-file source                              | Name recovery | Notes                              |
|------------|--------------------------------------------------|:-------------:|------------------------------------|
| NTFS       | `$MFT` records with the in-use flag cleared      | Full          | First target (milestone M1).       |
| FAT32      | Directory entries with the `0xE5` deletion marker| Partial       | First char of name is lost.        |
| exFAT      | Directory entry sets with the in-use bit cleared | Full          | Bitmap-based allocation.           |
| ext4       | Inodes/`orphan` list; journal replay for hints   | Partial       | Extents; some deletes wipe blocks. |

### NTFS (reference case)

NTFS is the primary target because its `$MFT` retains rich metadata for deleted files
until the record is reused. The parser:

1. Locates the `$MFT` via the boot sector.
2. Iterates MFT records, parsing attributes: `$STANDARD_INFORMATION` (timestamps),
   `$FILE_NAME` (name + parent reference), `$DATA` (resident bytes or non-resident
   runlist).
3. Reconstructs full paths by resolving parent references up to the root.
4. Marks records whose in-use flag is cleared as `Deleted`, and records whose parent is
   gone as `Orphaned` (recovered under a `lost+found`-style path).

Each of these is a separate, independently tested unit; MFT record parsing, attribute
parsing, and runlist decoding do not live in one function.

## Recoverability grading

Deleted metadata may be partly overwritten. Every entry carries a `recoverability`
verdict so the report and the user can distinguish:

- **Valid** — metadata intact and data clusters appear unallocated (high confidence).
- **Uncertain** — metadata intact but some data clusters may be reallocated.
- **Rejected** — metadata too damaged to trust; the region is left to the carve pass.

This grading is the handoff point to [hybrid orchestration](hybrid-orchestration.md):
what the filesystem cannot recover confidently becomes carving territory.

## Testing

- Parsers are tested against **small synthetic images** produced by `tools/`
  (deterministic, checked-in fixtures), covering live, deleted, and orphaned entries.
- Structure parsers (MFT records, directory entries, runlists/extents) have direct unit
  tests with hand-crafted byte fixtures, including deliberately corrupted metadata.
- Every metadata parser has a fuzz target — the same threat model as carving applies.
