// SPDX-License-Identifier: GPL-3.0-or-later
#include <bit>
#include <cstdint>

#include "SuperblockInternal.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ext4 {

namespace {

constexpr std::uint16_t kMagic = 0xEF53;

// Block size is `1024 << s_log_block_size`, and ext4 stops at 64 KiB.
constexpr std::uint32_t kMinBlockSize = 1024;
constexpr std::uint32_t kMaxBlockShift = 6;

// The smallest inode ext2 ever wrote, and the smallest a 64-bit volume's group
// descriptor may be.
constexpr std::uint32_t kMinInodeSize = 128;
constexpr std::uint32_t kSmallDescriptorSize = 32;
constexpr std::uint32_t kMinLargeDescriptorSize = 64;

constexpr std::uint32_t kBitsPerByte = 8;

// A size that is a power of two and fits between a floor and one block. Inode
// and group-descriptor sizes are both stated as such a size, and a volume whose
// records would not fit in a block cannot be read at all.
[[nodiscard]] bool
isBlockSizedPowerOfTwo(std::uint32_t value, std::uint32_t low, std::uint32_t blockSizeBytes) {
	return std::has_single_bit(value) && value >= low && value <= blockSizeBytes;
}

[[nodiscard]] Result<std::uint32_t> rejectAt(std::uint64_t offset) {
	return Error{.code = ErrorCode::kInvalidArgument, .offset = offset};
}

[[nodiscard]] Result<std::uint32_t> blockShift(const ByteReader& reader) {
	return reader.readLe<std::uint32_t>(kBlockShiftOffset).andThen([](std::uint32_t shift) {
		return shift > kMaxBlockShift ? rejectAt(kBlockShiftOffset) : Result<std::uint32_t>(shift);
	});
}

} // namespace

Result<bool> namesExt4(const ByteReader& reader) {
	return reader.readLe<std::uint16_t>(kMagicOffset).andThen([&reader](std::uint16_t magic) {
		if (magic != kMagic) {
			return Result<bool>(Error{.code = ErrorCode::kInvalidArgument, .offset = kMagicOffset});
		}
		return blockShift(reader).map([](std::uint32_t) { return true; });
	});
}

Result<std::uint32_t> blockSize(const ByteReader& reader) {
	return blockShift(reader).map([](std::uint32_t shift) { return kMinBlockSize << shift; });
}

// The superblock lives at byte 1024. On a 1024-byte-block volume that is a
// block of its own, so data starts at block 1; on every larger block size the
// superblock sits inside block 0 and data starts there.
Result<std::uint32_t> firstDataBlock(const ByteReader& reader, std::uint32_t blockSizeBytes) {
	const std::uint32_t expected = blockSizeBytes == kMinBlockSize ? 1U : 0U;
	return reader.readLe<std::uint32_t>(kFirstDataBlockOffset)
		.andThen([expected](std::uint32_t first) {
			return first != expected ? rejectAt(kFirstDataBlockOffset)
									 : Result<std::uint32_t>(first);
		});
}

// `offset` is which field to read and `blockSizeBytes` is what caps it — a
// diagnostic position beside a value, as in `fs::safeMul32`. Both are passed as
// named constants at every call site, so the swap the check warns about does not
// arise.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
Result<std::uint32_t>
perGroupCount(const ByteReader& reader, std::uint64_t offset, std::uint32_t blockSizeBytes) {
	const std::uint32_t cap = blockSizeBytes * kBitsPerByte;
	return reader.readLe<std::uint32_t>(offset).andThen([offset, cap](std::uint32_t count) {
		return count == 0 || count > cap ? rejectAt(offset) : Result<std::uint32_t>(count);
	});
}

// NOLINTEND(bugprone-easily-swappable-parameters)

Result<std::uint32_t> inodeSize(const ByteReader& reader, std::uint32_t blockSizeBytes) {
	return reader.readLe<std::uint16_t>(kInodeSizeOffset)
		.andThen([blockSizeBytes](std::uint16_t size) {
			if (!isBlockSizedPowerOfTwo(size, kMinInodeSize, blockSizeBytes)) {
				return rejectAt(kInodeSizeOffset);
			}
			return Result<std::uint32_t>(size);
		});
}

// Only a 64-bit volume states its descriptor size; on every other one the
// descriptors are 32 bytes whatever `s_desc_size` happens to hold.
Result<std::uint32_t> descriptorSize(const ByteReader& reader, const SuperblockFields& fields) {
	if ((fields.featureIncompat & kIncompat64Bit) == 0) {
		return kSmallDescriptorSize;
	}
	const auto blockSizeBytes = fields.blockSizeBytes;
	return reader.readLe<std::uint16_t>(kDescriptorSizeOffset)
		.andThen([blockSizeBytes](std::uint16_t size) {
			if (!isBlockSizedPowerOfTwo(size, kMinLargeDescriptorSize, blockSizeBytes)) {
				return rejectAt(kDescriptorSizeOffset);
			}
			return Result<std::uint32_t>(size);
		});
}

Result<std::uint32_t> inodeCount(const ByteReader& reader) {
	return reader.readLe<std::uint32_t>(kInodeCountOffset).andThen([](std::uint32_t count) {
		return count == 0 ? rejectAt(kInodeCountOffset) : Result<std::uint32_t>(count);
	});
}

// A 64-bit volume keeps the high half of its block count far from the low one,
// at 0x150 — the field was appended when the feature arrived.
Result<std::uint64_t> blockCount(const ByteReader& reader, std::uint32_t featureIncompat) {
	const bool wide = (featureIncompat & kIncompat64Bit) != 0;
	return reader.readLe<std::uint32_t>(kBlockCountOffset).andThen([&](std::uint32_t low) {
		const auto high =
			wide ? reader.readLe<std::uint32_t>(kBlockCountHighOffset) : Result<std::uint32_t>(0U);
		return high.andThen([low](std::uint32_t upper) -> Result<std::uint64_t> {
			const auto total = (std::uint64_t{upper} << 32U) | low;
			return total == 0 ? Result<std::uint64_t>(Error{
									.code = ErrorCode::kInvalidArgument,
									.offset = kBlockCountOffset})
							  : Result<std::uint64_t>(total);
		});
	});
}

} // namespace revenant::fs::ext4
