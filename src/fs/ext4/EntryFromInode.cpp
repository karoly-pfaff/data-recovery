// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ext4/EntryFromInode.hpp"

#include <cstdint>
#include <utility>
#include <vector>

#include "fs/ext4/ExtentWalk.hpp"
#include "fs/ext4/JournalHint.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ext4/Inode.hpp"

namespace revenant::fs::ext4 {

namespace {

// A name that is not a live directory entry, pointing at an inode that has links
// again, no longer describes what is on those blocks.
[[nodiscard]] bool reused(const FoundName& found, const Ext4Inode& inode) {
	return found.state != EntryState::kLive && !inode.isDeleted;
}

[[nodiscard]] std::vector<Extent>
fromJournal(const EntrySource& source, const FoundName& found, std::uint64_t sizeBytes) {
	const JournalSource hint{
		.blocks = source.blocks,
		.inodes = source.inodes,
		.journal = source.journal};
	const auto recovered = journalExtents(hint, found.inodeNumber, sizeBytes);
	return recovered.hasValue() ? recovered.value() : std::vector<Extent>{};
}

[[nodiscard]] std::vector<Extent>
locate(const EntrySource& source, const FoundName& found, const Ext4Inode& inode) {
	if (inode.sizeInBytes == 0 || reused(found, inode)) {
		return {};
	}
	const auto own = inodeExtents(*source.blocks, inode);
	if (own.hasValue()) {
		return own.value();
	}
	return fromJournal(source, found, inode.sizeInBytes);
}

// Grading is on metadata integrity alone. Only a live entry whose name decoded
// intact and whose content was located reaches the top grade; everything else is
// uncertain, and the carve pass covers it.
[[nodiscard]] Confidence
gradeOf(const FoundName& found, const Ext4Inode& inode, const std::vector<Extent>& extents) {
	const bool located = !extents.empty() || inode.sizeInBytes == 0;
	return found.state == EntryState::kLive && found.nameIsExact && located
			   ? Confidence::kValid
			   : Confidence::kUncertain;
}

} // namespace

RecoveredEntry entryOf(const EntrySource& source, const FoundName& found, const Ext4Inode& inode) {
	auto extents = locate(source, found, inode);
	const auto grade = gradeOf(found, inode, extents);
	return RecoveredEntry{
		.path = found.path,
		.sizeInBytes = inode.sizeInBytes,
		.extents = std::move(extents),
		.residentContent = {},
		.timestamps = inode.timestamps,
		.state = found.state,
		.recoverability = grade};
}

} // namespace revenant::fs::ext4
