// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ext4/Ext4FileSystem.hpp"

#include <cstddef>
#include <memory>
#include <span>

#include "SuperblockInternal.hpp"
#include "fs/MountRegion.hpp"
#include "fs/ext4/BlockReader.hpp"
#include "fs/ext4/DirectoryWalk.hpp"
#include "fs/ext4/EntryFromInode.hpp"
#include "fs/ext4/InodeTable.hpp"
#include "fs/ext4/Journal.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/ext4/Superblock.hpp"

namespace revenant::fs::ext4 {

namespace {

class Ext4FileSystem final : public FileSystem {
public:
	Ext4FileSystem(BlockDevice& device, const Ext4Geometry& geometry)
		: blocks_(device, geometry), inodes_(blocks_), journal_(readJournal(blocks_, inodes_)) {}

	[[nodiscard]] Result<EnumerationStats> enumerate(EntryVisitor& visitor) const override {
		const EntrySource source{.blocks = &blocks_, .inodes = &inodes_, .journal = &journal_};
		return walkVolume(source, visitor);
	}

private:
	Ext4Blocks blocks_;
	Ext4InodeTable inodes_;
	// Indexed once at mount, because every deleted file whose tree was wiped
	// asks it the same kind of question and the journal does not change under a
	// read-only pass.
	Journal journal_;
};

// ext4 names itself with sixteen bits of magic a kilobyte into the volume, so
// the block size beside it is checked as part of the recognition. Anything else
// is another filesystem's volume rather than a broken ext4 one, and is handed
// back to the mount table to keep looking.
[[nodiscard]] Result<Ext4Geometry> recognize(std::span<const std::byte> superblock) {
	if (superblock.size() < kSuperblockBytes) {
		return Error{.code = ErrorCode::kNotFound};
	}
	if (!namesExt4(ByteReader{superblock.first(kSuperblockBytes)}).hasValue()) {
		return Error{.code = ErrorCode::kNotFound};
	}
	return parseExt4Superblock(superblock);
}

} // namespace

Result<std::unique_ptr<FileSystem>> mountExt4(BlockDevice& device) {
	return readMountRegion(
			   device,
			   MountRegion{.offset = kSuperblockOffset, .length = kSuperblockBytes})
		.andThen(recognize)
		.andThen([&device](const Ext4Geometry& geometry) -> Result<std::unique_ptr<FileSystem>> {
			return std::unique_ptr<FileSystem>{std::make_unique<Ext4FileSystem>(device, geometry)};
		});
}

} // namespace revenant::fs::ext4
