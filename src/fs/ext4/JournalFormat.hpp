// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. jbd2's on-disk vocabulary: the journal's own superblock and the
// descriptor blocks that say which filesystem block each journalled copy
// belongs to. Big-endian throughout — the journal predates ext4 and kept the
// byte order the original jbd used. Not a public interface.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"

namespace revenant::fs::ext4 {

// Every jbd2 block starts with this header, magic included.
inline constexpr std::size_t kJournalHeaderBytes = 12;
inline constexpr std::uint32_t kJournalMagic = 0xC03B'3998;

inline constexpr std::uint32_t kDescriptorBlockType = 1;
inline constexpr std::uint32_t kSuperblockV1Type = 3;
inline constexpr std::uint32_t kSuperblockV2Type = 4;

// What a journal needs to be read, once its superblock has been believed.
struct JournalHead {
	std::uint32_t blockSizeBytes;
	std::uint32_t maxBlocks;
	// The first block a transaction may use; everything below it is the
	// journal's own bookkeeping.
	std::uint32_t firstBlock;
	// How wide one descriptor tag is here. jbd2 has three shapes, chosen by the
	// journal's feature flags, and reading one as another yields block numbers
	// that address the wrong blocks entirely.
	std::size_t tagBytes;
	// Whether a descriptor block keeps a four-byte checksum at its end, which is
	// not a tag and must not be read as one.
	bool hasBlockTail;
};

// One block a descriptor block was announcing: the filesystem block the copy
// belongs to, and how far after the descriptor that copy sits.
struct TaggedBlock {
	std::uint64_t fileSystemBlock;
	std::uint32_t blocksAfterDescriptor;
};

// Reads the journal superblock at the front of `block`.
//
// A wrong magic is `kInvalidArgument` at `0x00` and a block type that is not a
// superblock `kInvalidArgument` at `0x04`. A feature flag this build does not
// implement is `kInvalidArgument` at `0x28`: a journal is either read the way it
// was written or not read at all, and guessing at a tag layout would produce
// block numbers that look perfectly plausible.
[[nodiscard]] Result<JournalHead> parseJournalSuperblock(std::span<const std::byte> block);

// The blocks a descriptor block announces, in the order its data blocks follow.
//
// A block that is not a descriptor is `kInvalidArgument` at `0x04`. A tag list
// that runs to the end of the block without a last-tag marker is taken as far as
// it fits: what a journal claims is data like any other.
[[nodiscard]] Result<std::vector<TaggedBlock>>
parseDescriptorTags(std::span<const std::byte> block, const JournalHead& head);

} // namespace revenant::fs::ext4
