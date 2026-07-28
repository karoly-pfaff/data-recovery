// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "revenant/core/Result.hpp"
#include "revenant/fs/RecoveredEntry.hpp"

namespace revenant::fs {

// What one walk of a volume got through. `recordsScanned` counts the metadata
// slots the walk looked at, in whatever unit the filesystem keeps its
// bookkeeping in — MFT records, directory entries, inodes.
struct EnumerationStats {
	std::uint64_t recordsScanned;
	std::uint64_t entriesReported;

	// True when the volume's own metadata is not what a conforming formatter
	// writes — a FAT32 volume below the 65525-cluster minimum, say. It parsed
	// and it was read, so this is a warning the operator is given rather than a
	// reason to refuse: the alternative would throw away files that are plainly
	// there. Filesystems with nothing of the kind to check leave it false.
	bool nonConformingVolume;
};

// A volume that has already been recognized: geometry parsed, tables located,
// ready to be walked. Mounting is the parse and produces this; enumerating is
// the traversal and is all this offers.
//
// Discovery only — there is no method here that returns bytes, because nothing
// is extracted and nothing is written at this layer (ADR-0006). The device the
// mount was made from is borrowed, never owned, and must outlive it.
class FileSystem {
public:
	virtual ~FileSystem() = default;
	FileSystem() = default;
	FileSystem(const FileSystem&) = delete;
	FileSystem& operator=(const FileSystem&) = delete;
	FileSystem(FileSystem&&) = delete;
	FileSystem& operator=(FileSystem&&) = delete;

	// Reports every file the volume holds — live, deleted, or orphaned — to
	// `visitor`. A metadata slot that will not parse is skipped rather than
	// fatal: that region is what the carve pass exists for. A device read fault
	// is fatal, because a disk that will not read is not a disk with no files.
	[[nodiscard]] virtual Result<EnumerationStats> enumerate(EntryVisitor& visitor) const = 0;
};

} // namespace revenant::fs
