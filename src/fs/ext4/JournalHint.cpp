// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ext4/JournalHint.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fs/ext4/ExtentWalk.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ext4/Inode.hpp"

namespace revenant::fs::ext4 {

namespace {

// Where an inode sits: the filesystem block that holds it, and how far into it.
// The block is what the journal indexes; the offset is what picks the inode out
// of the copy.
struct InodeSite {
	std::uint64_t block;
	std::uint64_t withinBlock;
};

[[nodiscard]] Result<InodeSite> siteOf(const JournalSource& source, std::uint32_t number) {
	const std::uint64_t blockBytes = source.blocks->geometry().blockSizeBytes;
	return source.inodes->offsetOf(number).map([blockBytes](std::uint64_t at) {
		return InodeSite{.block = at / blockBytes, .withinBlock = at % blockBytes};
	});
}

// The inode as one journalled copy of its block still holds it.
[[nodiscard]] Result<Ext4Inode>
inodeInCopy(const JournalSource& source, std::uint64_t copyOffset, std::uint64_t withinBlock) {
	std::vector<std::byte> slot(source.blocks->geometry().inodeSizeBytes, std::byte{0});
	return source.blocks->read(copyOffset + withinBlock, slot).andThen([&slot](std::size_t) {
		return parseExt4Inode(slot);
	});
}

// A copy is a hint only if it maps something. A journalled copy of the *same*
// zeroed inode is what the deletion itself wrote, and it is no more use than the
// one on disk.
[[nodiscard]] Result<std::vector<Extent>>
hintFrom(const JournalSource& source, const Ext4Inode& inode, std::uint64_t sizeBytes) {
	if (!inode.usesExtents) {
		return Error{.code = ErrorCode::kNotFound};
	}
	return treeExtents(*source.blocks, inode.blockMap, sizeBytes)
		.andThen([](const std::vector<Extent>& extents) -> Result<std::vector<Extent>> {
			if (extents.empty()) {
				return Error{.code = ErrorCode::kNotFound};
			}
			return extents;
		});
}

[[nodiscard]] Result<std::vector<Extent>>
firstUsableCopy(const JournalSource& source, const InodeSite& site, std::uint64_t sizeBytes) {
	for (const std::uint64_t copyOffset : source.journal->copiesOf(site.block)) {
		const auto found =
			inodeInCopy(source, copyOffset, site.withinBlock).andThen([&](const Ext4Inode& inode) {
				return hintFrom(source, inode, sizeBytes);
			});
		if (found.hasValue()) {
			return found;
		}
	}
	return Error{.code = ErrorCode::kNotFound};
}

} // namespace

Result<std::vector<Extent>>
journalExtents(const JournalSource& source, std::uint32_t number, std::uint64_t sizeBytes) {
	if (!source.journal->known() || sizeBytes == 0) {
		return Error{.code = ErrorCode::kNotFound};
	}
	return siteOf(source, number).andThen([&source, sizeBytes](const InodeSite& site) {
		return firstUsableCopy(source, site, sizeBytes);
	});
}

} // namespace revenant::fs::ext4
