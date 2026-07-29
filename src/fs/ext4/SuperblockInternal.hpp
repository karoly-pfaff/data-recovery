// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal helpers shared by the ext4 superblock parser's translation units:
// one reader per field, the raw block they fill, and the derivation that turns
// it into geometry. Each reader validates its own row and reports that field's
// byte offset on rejection. Not a public interface.

#include <cstdint>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/ext4/Superblock.hpp"

namespace revenant::fs::ext4 {

// The offsets a rejection names.
inline constexpr std::uint64_t kInodeCountOffset = 0x00;
inline constexpr std::uint64_t kBlockCountOffset = 0x04;
inline constexpr std::uint64_t kFirstDataBlockOffset = 0x14;
inline constexpr std::uint64_t kBlockShiftOffset = 0x18;
inline constexpr std::uint64_t kBlocksPerGroupOffset = 0x20;
inline constexpr std::uint64_t kInodesPerGroupOffset = 0x28;
inline constexpr std::uint64_t kMagicOffset = 0x38;
inline constexpr std::uint64_t kInodeSizeOffset = 0x58;
inline constexpr std::uint64_t kFeatureIncompatOffset = 0x60;
inline constexpr std::uint64_t kLastOrphanOffset = 0x64;
inline constexpr std::uint64_t kDescriptorSizeOffset = 0xFE;
inline constexpr std::uint64_t kBlockCountHighOffset = 0x150;

// The two feature bits that change how the volume is *read*, as opposed to the
// many that only change how it is written.
inline constexpr std::uint32_t kIncompatExtents = 0x40;
inline constexpr std::uint32_t kIncompat64Bit = 0x80;

// What the superblock says, each field already validated on its own terms. The
// derivation into geometry is `geometryOf`.
struct SuperblockFields {
	std::uint32_t blockSizeBytes;
	std::uint32_t totalInodes;
	std::uint64_t totalBlocks;
	std::uint32_t firstDataBlock;
	std::uint32_t blocksPerGroup;
	std::uint32_t inodesPerGroup;
	std::uint32_t inodeSizeBytes;
	std::uint32_t descriptorSizeBytes;
	std::uint32_t featureIncompat;
	std::uint32_t lastOrphanInode;
};

// Whether the block names ext4: the `0xEF53` magic *and* a block-size shift
// ext4 can express. The magic alone is sixteen bits — a coincidence a RAW
// volume can produce — and a mounter that claims a volume owns its answer, so
// the shift is checked with it. This is the question the mount table probes
// with.
[[nodiscard]] Result<bool> namesExt4(const ByteReader& reader);

[[nodiscard]] Result<std::uint32_t> blockSize(const ByteReader& reader);
[[nodiscard]] Result<std::uint32_t>
firstDataBlock(const ByteReader& reader, std::uint32_t blockSizeBytes);

// Blocks or inodes per group, whichever `offset` names. One block group's
// bitmap is one block, so neither may exceed what that many bits can address.
[[nodiscard]] Result<std::uint32_t>
perGroupCount(const ByteReader& reader, std::uint64_t offset, std::uint32_t blockSizeBytes);

[[nodiscard]] Result<std::uint32_t>
inodeSize(const ByteReader& reader, std::uint32_t blockSizeBytes);
[[nodiscard]] Result<std::uint32_t>
descriptorSize(const ByteReader& reader, const SuperblockFields& fields);
[[nodiscard]] Result<std::uint32_t> inodeCount(const ByteReader& reader);
[[nodiscard]] Result<std::uint64_t>
blockCount(const ByteReader& reader, std::uint32_t featureIncompat);

// Restates validated fields as geometry, deriving how many block groups the
// volume holds and where their descriptors begin.
[[nodiscard]] Result<Ext4Geometry> geometryOf(const SuperblockFields& fields);

} // namespace revenant::fs::ext4
