// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ext4/Journal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "fs/ExtentLocate.hpp"
#include "fs/ext4/BlockReader.hpp"
#include "fs/ext4/ExtentWalk.hpp"
#include "fs/ext4/InodeTable.hpp"
#include "fs/ext4/JournalFormat.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ext4/Inode.hpp"
#include "revenant/fs/ext4/Superblock.hpp"

namespace revenant::fs::ext4 {

namespace {

// The journal file, addressed in its own block numbering. It is a file like any
// other — inode 8 — so reaching its Nth block means asking its extents where
// that block landed on the device.
class JournalFile {
public:
	JournalFile(const Ext4Blocks& blocks, std::vector<Extent> extents) noexcept
		: blocks_(&blocks), extents_(std::move(extents)) {}

	[[nodiscard]] std::uint64_t blockCount() const {
		return extentBytesOf() / blocks_->geometry().blockSizeBytes;
	}

	[[nodiscard]] Result<std::uint64_t> offsetOf(std::uint64_t journalBlock) const {
		const std::uint64_t blockBytes = blocks_->geometry().blockSizeBytes;
		return locateInExtents(
			extents_,
			FileRange{.offset = journalBlock * blockBytes, .length = blockBytes});
	}

	[[nodiscard]] Result<std::vector<std::byte>> read(std::uint64_t journalBlock) const {
		return offsetOf(journalBlock).andThen([this](std::uint64_t at) {
			std::vector<std::byte> bytes(blocks_->geometry().blockSizeBytes, std::byte{0});
			return blocks_->read(at, bytes).map([&bytes](std::size_t) { return bytes; });
		});
	}

private:
	[[nodiscard]] std::uint64_t extentBytesOf() const {
		std::uint64_t total = 0;
		for (const Extent& extent : extents_) {
			total += extent.lengthBytes;
		}
		return total;
	}

	const Ext4Blocks* blocks_; // non-owning, never null
	std::vector<Extent> extents_;
};

// Where one descriptor block's announced copies actually lie.
void collectCopies(
	std::vector<JournalCopy>& copies,
	const JournalFile& file,
	std::uint64_t descriptorBlock,
	const std::vector<TaggedBlock>& tags) {
	for (const TaggedBlock& tag : tags) {
		const auto where = file.offsetOf(descriptorBlock + tag.blocksAfterDescriptor);
		if (where.hasValue()) {
			copies.push_back(
				JournalCopy{.fileSystemBlock = tag.fileSystemBlock, .deviceOffset = where.value()});
		}
	}
}

// One block of the sweep. Returns how far the cursor advances: a descriptor
// block is followed by the data blocks it announced, so the cursor jumps past
// them; anything else costs one block.
[[nodiscard]] std::uint64_t takeBlock(
	std::vector<JournalCopy>& copies,
	const JournalFile& file,
	std::uint64_t at,
	const JournalHead& head) {
	const auto block = file.read(at);
	if (!block.hasValue()) {
		return 1;
	}
	const auto tags = parseDescriptorTags(block.value(), head);
	if (!tags.hasValue() || tags.value().empty()) {
		return 1;
	}
	collectCopies(copies, file, at, tags.value());
	return tags.value().size() + 1;
}

// The journal swept block by block rather than followed from its head. A
// transaction this build would not replay still *wrote* the copy it carries, and
// an older copy is exactly what the hint is after — so every descriptor the
// journal still holds is read, whatever sequence it belongs to.
[[nodiscard]] std::vector<JournalCopy> sweep(const JournalFile& file, const JournalHead& head) {
	std::vector<JournalCopy> copies;
	const auto last = std::min(file.blockCount(), kMaxJournalBlocks);
	std::uint64_t at = head.firstBlock;
	while (at < last && copies.size() < kMaxJournalCopies) {
		at += takeBlock(copies, file, at, head);
	}
	return copies;
}

[[nodiscard]] Result<std::vector<Extent>>
journalFileExtents(const Ext4Blocks& blocks, const Ext4InodeTable& inodes) {
	return inodes.read(kJournalInode).andThen([&blocks](const Ext4Inode& inode) {
		return inodeExtents(blocks, inode);
	});
}

// A journal laid out for a different block size than the volume around it is
// not this volume's journal, whatever it says.
[[nodiscard]] bool describesVolume(const JournalHead& head, const Ext4Geometry& geometry) {
	return head.blockSizeBytes == geometry.blockSizeBytes && head.firstBlock > 0;
}

// The journal's own superblock is its block 0. Anything the volume disagrees
// with there ends the read: what follows is only worth sweeping if this really
// is the journal it belongs to.
[[nodiscard]] Result<JournalHead> headOf(const JournalFile& file, const Ext4Geometry& geometry) {
	return file.read(0)
		.andThen([](const std::vector<std::byte>& block) { return parseJournalSuperblock(block); })
		.andThen([&geometry](const JournalHead& head) -> Result<JournalHead> {
			if (!describesVolume(head, geometry)) {
				return Error{.code = ErrorCode::kInvalidArgument, .offset = head.blockSizeBytes};
			}
			return head;
		});
}

} // namespace

Journal::Journal(std::vector<JournalCopy> copies) noexcept : copies_(std::move(copies)) {}

bool Journal::known() const noexcept {
	return !copies_.empty();
}

std::vector<std::uint64_t> Journal::copiesOf(std::uint64_t block) const {
	std::vector<std::uint64_t> offsets;
	for (const JournalCopy& copy : copies_) {
		if (copy.fileSystemBlock == block) {
			offsets.push_back(copy.deviceOffset);
		}
	}
	return offsets;
}

Journal readJournal(const Ext4Blocks& blocks, const Ext4InodeTable& inodes) {
	const auto extents = journalFileExtents(blocks, inodes);
	if (!extents.hasValue()) {
		return Journal{};
	}
	const JournalFile file{blocks, extents.value()};
	const auto head = headOf(file, blocks.geometry());
	return head.hasValue() ? Journal{sweep(file, head.value())} : Journal{};
}

} // namespace revenant::fs::ext4
