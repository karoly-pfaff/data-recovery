// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ext4/Inode.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "fs/SlotReader.hpp"
#include "fs/UnixTime.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs::ext4 {

namespace {

constexpr std::size_t kModeOffset = 0x00;
constexpr std::size_t kSizeLowOffset = 0x04;
constexpr std::size_t kAccessTimeOffset = 0x08;
constexpr std::size_t kModifyTimeOffset = 0x10;
constexpr std::size_t kDeleteTimeOffset = 0x14;
constexpr std::size_t kLinkCountOffset = 0x1A;
constexpr std::size_t kFlagsOffset = 0x20;
constexpr std::size_t kBlockMapOffset = 0x28;
constexpr std::size_t kSizeHighOffset = 0x6C;
constexpr std::size_t kExtraSizeOffset = 0x80;
constexpr std::size_t kCreateTimeOffset = 0x90;

// The high nibble of `i_mode` is the file type; the rest is permissions.
constexpr std::uint16_t kFormatMask = 0xF000;
constexpr std::uint16_t kDirectoryFormat = 0x4000;
constexpr std::uint16_t kRegularFileFormat = 0x8000;

// EXT4_EXTENTS_FL: `i_block` is an extent tree rather than ext2's indirect list.
constexpr std::uint32_t kExtentsFlag = 0x0008'0000;

// `i_crtime` ends at 0x94, and the extra area it lives in starts at 128 — so an
// inode has to be that long *and* declare at least twenty bytes of extra area
// before the field is anything but whatever the formatter left there.
constexpr std::uint16_t kExtraBytesForCreateTime = 20;
constexpr std::size_t kInodeBytesForCreateTime = 0x94;

[[nodiscard]] std::uint64_t stampAt(const ByteReader& reader, std::size_t offset) {
	return filetimeFromUnixSeconds(slotFieldAt<std::uint32_t>(reader, offset));
}

[[nodiscard]] bool hasCreateTime(const ByteReader& reader) {
	return reader.size() >= kInodeBytesForCreateTime &&
		   slotFieldAt<std::uint16_t>(reader, kExtraSizeOffset) >= kExtraBytesForCreateTime;
}

// ext4's `i_ctime` is when the inode last changed, not when the file was made,
// so `created` comes from `i_crtime` or from nowhere at all.
[[nodiscard]] Timestamps timestampsOf(const ByteReader& reader) {
	return Timestamps{
		.created = hasCreateTime(reader) ? stampAt(reader, kCreateTimeOffset) : 0,
		.modified = stampAt(reader, kModifyTimeOffset),
		.accessed = stampAt(reader, kAccessTimeOffset)};
}

// `i_size_high` is `i_dir_acl` for anything that is not a regular file.
[[nodiscard]] std::uint64_t sizeOf(const ByteReader& reader, bool isRegularFile) {
	const std::uint64_t low = slotFieldAt<std::uint32_t>(reader, kSizeLowOffset);
	if (!isRegularFile) {
		return low;
	}
	return (std::uint64_t{slotFieldAt<std::uint32_t>(reader, kSizeHighOffset)} << 32U) | low;
}

[[nodiscard]] std::array<std::byte, kBlockMapBytes> blockMapOf(std::span<const std::byte> slot) {
	std::array<std::byte, kBlockMapBytes> map{};
	std::ranges::copy(slot.subspan(kBlockMapOffset, kBlockMapBytes), map.begin());
	return map;
}

[[nodiscard]] Ext4Inode inodeOf(const ByteReader& reader, std::span<const std::byte> slot) {
	const auto mode = slotFieldAt<std::uint16_t>(reader, kModeOffset);
	const auto links = slotFieldAt<std::uint16_t>(reader, kLinkCountOffset);
	const auto flags = slotFieldAt<std::uint32_t>(reader, kFlagsOffset);
	const bool regular = (mode & kFormatMask) == kRegularFileFormat;
	return Ext4Inode{
		.mode = mode,
		.linkCount = links,
		.sizeInBytes = sizeOf(reader, regular),
		.flags = flags,
		.deletionTime = slotFieldAt<std::uint32_t>(reader, kDeleteTimeOffset),
		.timestamps = timestampsOf(reader),
		.blockMap = blockMapOf(slot),
		.isDirectory = (mode & kFormatMask) == kDirectoryFormat,
		.isRegularFile = regular,
		.usesExtents = (flags & kExtentsFlag) != 0,
		.isDeleted = links == 0,
		.isUnused = mode == 0};
}

} // namespace

Result<Ext4Inode> parseExt4Inode(std::span<const std::byte> slot) {
	if (slot.size() < kMinInodeBytes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = slot.size()};
	}
	const ByteReader reader{slot};
	return inodeOf(reader, slot);
}

} // namespace revenant::fs::ext4
