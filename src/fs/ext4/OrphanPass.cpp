// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ext4/OrphanPass.hpp"

#include <algorithm>
#include <cstdint>
#include <span>

#include "fs/ext4/BlockReader.hpp"
#include "fs/ext4/EntryFromInode.hpp"
#include "fs/ext4/InodeTable.hpp"
#include "fs/ext4/OrphanList.hpp"
#include "fs/ext4/WalkCursor.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/ext4/Inode.hpp"
#include "revenant/fs/ext4/Superblock.hpp"

namespace revenant::fs::ext4 {

namespace {

[[nodiscard]] bool usable(
	const Result<Ext4Inode>& inode,
	std::uint32_t number,
	std::span<const std::uint32_t> alreadyReported) {
	return inode.hasValue() && !inode.value().isUnused && !inode.value().isDirectory &&
		   std::ranges::find(alreadyReported, number) == alreadyReported.end();
}

[[nodiscard]] std::uint64_t reportOne(
	const EntrySource& source,
	EntryVisitor& visitor,
	std::uint32_t number,
	std::span<const std::uint32_t> alreadyReported) {
	const auto inode = source.inodes->read(number);
	if (!usable(inode, number, alreadyReported)) {
		return 0;
	}
	visitor.onEntry(entryOf(source, orphanName(number), inode.value()));
	return 1;
}

} // namespace

OrphanPassResult reportOrphans(
	const EntrySource& source,
	EntryVisitor& visitor,
	std::span<const std::uint32_t> alreadyReported) {
	OrphanPassResult result{.scanned = 0, .reported = 0};
	const auto head = source.blocks->geometry().lastOrphanInode;
	for (const std::uint32_t number : orphanInodes(*source.inodes, head)) {
		++result.scanned;
		result.reported += reportOne(source, visitor, number, alreadyReported);
	}
	return result;
}

} // namespace revenant::fs::ext4
