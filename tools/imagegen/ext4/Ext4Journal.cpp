// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ext4/Ext4Journal.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "imagegen/ByteWriter.hpp"
#include "imagegen/ext4/Ext4Layout.hpp"

namespace revenant::imagegen::ext4 {

namespace {

constexpr std::uint32_t kJournalMagic = 0xC03B'3998;
constexpr std::uint32_t kDescriptorBlockType = 1;
constexpr std::uint32_t kSuperblockV2Type = 4;

// The journal's own first usable block: block 0 is its superblock.
constexpr std::uint32_t kFirstTransactionBlock = 1;

// The single tag says it shares the transaction's UUID — so no UUID follows it —
// and that it is the last.
constexpr std::uint16_t kSameUuidAndLast = 0x0A;

void putBlockHeader(std::vector<std::byte>& block, std::uint32_t blockType) {
	putBe<std::uint32_t>(block, 0x00, kJournalMagic);
	putBe<std::uint32_t>(block, 0x04, blockType);
	putBe<std::uint32_t>(block, 0x08, 1); // h_sequence
}

[[nodiscard]] std::vector<std::byte> journalSuperblock(std::size_t blockSizeBytes) {
	std::vector<std::byte> block(blockSizeBytes, std::byte{0});
	putBlockHeader(block, kSuperblockV2Type);
	putBe<std::uint32_t>(block, 0x0C, static_cast<std::uint32_t>(blockSizeBytes));
	putBe<std::uint32_t>(block, 0x10, kJournalBlocks);
	putBe<std::uint32_t>(block, 0x14, kFirstTransactionBlock);
	putBe<std::uint32_t>(block, 0x18, 1); // s_sequence
	putBe<std::uint32_t>(block, 0x1C, kFirstTransactionBlock);
	return block;
}

// `fileSystemBlock` is which block the tag names and `blockSizeBytes` is how
// large the block holding the tag is — a subject beside a dimension, passed as
// named values at the one call site below.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
[[nodiscard]] std::vector<std::byte>
descriptorBlock(std::uint32_t fileSystemBlock, std::size_t blockSizeBytes) {
	std::vector<std::byte> block(blockSizeBytes, std::byte{0});
	putBlockHeader(block, kDescriptorBlockType);
	putBe<std::uint32_t>(block, 0x0C, fileSystemBlock); // t_blocknr
	putBe<std::uint16_t>(block, 0x12, kSameUuidAndLast);
	return block;
}

// NOLINTEND(bugprone-easily-swappable-parameters)

} // namespace

std::vector<std::vector<std::byte>>
journalBlocks(const RememberedBlock& remembered, std::size_t blockSizeBytes) {
	std::vector<std::vector<std::byte>> blocks;
	blocks.push_back(journalSuperblock(blockSizeBytes));
	blocks.push_back(descriptorBlock(remembered.fileSystemBlock, blockSizeBytes));
	blocks.push_back(remembered.content);
	return blocks;
}

} // namespace revenant::imagegen::ext4
