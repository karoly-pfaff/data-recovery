<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Filesystem Layer

The filesystem layer powers `revenant-undelete`: it reads on-disk metadata to recover
deleted files **with their original names, paths, and timestamps** — the information
carving alone can never reconstruct. All parsers are strictly **read-only**.

## The vocabulary

```cpp
namespace revenant::fs {

struct RecoveredEntry {
    std::string path;                        // volume-relative, '/'-separated UTF-8
    std::uint64_t sizeInBytes;
    std::vector<Extent> extents;             // where the data lives on the device
    std::vector<std::byte> residentContent;  // ...or the bytes themselves
    Timestamps timestamps;                   // created / modified / accessed
    EntryState state;                        // Live, Deleted, Orphaned
    Confidence recoverability;               // how intact the metadata is
};

class EntryVisitor {
public:
    virtual void onEntry(const RecoveredEntry& entry) = 0;
};

} // namespace revenant::fs
```

`path` is a **logical** path inside the volume, not a host path. It becomes one only
through `recovery::sanitizeOutputPath`
([ADR-0009](adr/adr-0009-output-safety.md)) — the single place a name off a disk may
reach the filesystem.

Content is either `extents` or `residentContent`, never both. A small file's bytes live
inside its metadata record, where the update-sequence fixup interrupts them, so they
cannot be named as a device extent and are carried as parsed. Both stay empty when the
metadata survived but the content could not be located — the region is then carve
territory rather than approximated bytes.

Enumeration reports to a visitor and never extracts
([ADR-0006](adr/adr-0006-candidate-arbitration-deferred-extraction.md)). Everything is
constructed over a `BlockDevice` and a byte range (a partition located by the
[volume layer](overview.md)); nothing writes.

`Timestamps` carries NTFS FILETIME ticks whatever the filesystem — the vocabulary needs
one epoch, and this is the widest and finest of the four. FAT's DOS time (2 s resolution,
1980–2107) and ext4's Unix seconds both convert into it without loss; the reverse would
not hold. Each parser converts on its way out.

## The seam

```cpp
namespace revenant::fs {

class FileSystem {
public:
    virtual Result<EnumerationStats> enumerate(EntryVisitor& visitor) const = 0;
};

Result<std::unique_ptr<FileSystem>> mountVolume(BlockDevice& device);

} // namespace revenant::fs
```

A `FileSystem` is what a **successful mount** returns: geometry parsed, tables located,
ready to walk. Mounting is the parse; enumerating is the traversal. Nothing here returns
bytes — extraction is a later layer's job
([ADR-0006](adr/adr-0006-candidate-arbitration-deferred-extraction.md)).

`mountVolume` offers the volume to each filesystem in a fixed, ordered probe table. A
mounter that does not find its own signature declines with `kNotFound` and the next is
asked; one that *does* find it owns the answer, and its parse failure is reported
unchanged. A corrupt NTFS volume is not an unknown volume. `kNotFound` from `mountVolume`
itself therefore means nothing recognized this volume at all — the formatted or RAW case,
which is what the carve pass exists for.

Order is a correctness property: an exFAT volume also carries a FAT-shaped BPB, so exFAT
is probed before FAT32.

The seam arrived with the **second** filesystem, not the first
([story-0029](../backlog/stories/story-0029-filesystem-seam.md)). One implementation did
not justify the abstraction, and inventing it before there was anything to vary it against
would have been guesswork.

## Supported filesystems

| Filesystem | Deleted-file source                              | Name recovery | Notes                              |
|------------|--------------------------------------------------|:-------------:|------------------------------------|
| NTFS       | `$MFT` records with the in-use flag cleared      | Full          | First target (milestone M1).       |
| FAT32      | Directory entries with the `0xE5` deletion marker| Partial       | First char lost; freed chain.      |
| exFAT      | Directory entry sets with the in-use bit cleared | Full          | Bitmap-based allocation.           |
| ext4       | Inodes/`orphan` list; journal replay for hints   | Partial       | Extents; some deletes wipe blocks. |

### NTFS (reference case)

NTFS is the primary target because its `$MFT` retains rich metadata for deleted files
until the record is reused. The parser:

1. Locates the `$MFT` via the boot sector. The `$MFT` is itself a file, so its record 0
   is read, its own `$DATA` runlist decoded, and the table addressed through the
   resulting extents — a fragmented `$MFT` is read correctly, not assumed contiguous.
2. Iterates MFT records from 16 (0–15 are the filesystem's own metadata files), parsing
   attributes: `$STANDARD_INFORMATION` (timestamps), `$FILE_NAME` (name + parent
   reference), `$DATA` (resident bytes or non-resident runlist).
3. Reconstructs full paths by resolving parent references up to the root (record 5). The
   chain is on-disk data, so the walk is depth-bounded and checks each parent's sequence
   number: NTFS bumps it when a slot is reused, which is how a stale reference is told
   from a live one.
4. Marks records whose in-use flag is cleared as `Deleted`, and records whose parent
   chain does not reach the root as `Orphaned`. Where an orphan is *written* (a
   `lost+found`-style path) is the sink's policy, not the parser's.

Each of these is a separate, independently tested unit; MFT record parsing, attribute
parsing, and runlist decoding do not live in one function. A record slot that will not
parse is skipped rather than fatal — an empty or destroyed slot is exactly what the
carve pass is for — while a device read fault stops the walk as a typed error.

## Recoverability grading

Deleted metadata may be partly overwritten. Every entry carries a `recoverability`
verdict so the report and the user can distinguish:

- **Valid** — the record parsed cleanly, its path reached the root, and its content was
  locatable.
- **Uncertain** — anything less: a damaged record, a broken parent chain, or a `$DATA`
  whose runs will not map (sparse, or reaching past the volume).
- **Rejected** — metadata too damaged to trust; such a record is never reported as an
  entry at all, and the region is left to the carve pass.

Grading is on **metadata integrity alone**. Whether a deleted file's clusters have since
been reallocated needs `$Bitmap`, which the vertical slice does not parse; that question
belongs to the entry's `state`, not to how far its metadata can be trusted.

This grading is the handoff point to [hybrid orchestration](hybrid-orchestration.md):
what the filesystem cannot recover confidently becomes carving territory.

## Testing

- Parsers are tested against **small synthetic images** produced by `tools/`
  (deterministic, checked-in fixtures), covering live, deleted, and orphaned entries.
- Structure parsers (MFT records, directory entries, runlists/extents) have direct unit
  tests with hand-crafted byte fixtures, including deliberately corrupted metadata.
- Every metadata parser has a fuzz target — the same threat model as carving applies.
