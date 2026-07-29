// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ext4/GroupDescriptor.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "fs/SlotReader.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ext4 {

namespace {

constexpr std::size_t kInodeTableLowOffset = 0x08;
constexpr std::size_t kInodeTableHighOffset = 0x28;

// The high half only exists on a volume whose descriptors were widened for it.
[[nodiscard]] std::uint64_t inodeTableBlock(const ByteReader& reader, std::size_t descriptorBytes) {
	const std::uint64_t low = slotFieldAt<std::uint32_t>(reader, kInodeTableLowOffset);
	if (descriptorBytes < kWideDescriptorBytes) {
		return low;
	}
	return (std::uint64_t{slotFieldAt<std::uint32_t>(reader, kInodeTableHighOffset)} << 32U) | low;
}

} // namespace

Result<Ext4Group>
parseGroupDescriptor(std::span<const std::byte> slot, std::size_t descriptorBytes) {
	if (descriptorBytes < kSmallDescriptorBytes) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = descriptorBytes};
	}
	return slotReader(slot, descriptorBytes).map([descriptorBytes](const ByteReader& reader) {
		return Ext4Group{.inodeTableBlock = inodeTableBlock(reader, descriptorBytes)};
	});
}

} // namespace revenant::fs::ext4
