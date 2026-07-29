// SPDX-License-Identifier: GPL-3.0-or-later
#include "volume/SectorIo.hpp"

#include <cstdint>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::volume {

Result<std::uint64_t> byteOffsetOf(const BlockDevice& device, std::uint64_t lba) {
	const std::uint64_t sectorSize = device.sectorSize();
	if (sectorSize == 0) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = 0};
	}
	// The division is the bound *and* the overflow proof: past it, `lba` is small
	// enough that the product below stays inside the device.
	if (lba >= device.sizeInBytes() / sectorSize) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = device.sizeInBytes()};
	}
	return lba * sectorSize;
}

Result<TableSector> readTableSector(BlockDevice& device, std::uint64_t lba) {
	return byteOffsetOf(device, lba).andThen([&device](std::uint64_t offset) {
		TableSector sector{};
		const auto read = device.readAt(offset, sector);
		if (!read.hasValue()) {
			return Result<TableSector>(read.error());
		}
		if (read.value() < sector.size()) {
			return Result<TableSector>(Error{.code = ErrorCode::kOutOfRange, .offset = offset});
		}
		return Result<TableSector>(sector);
	});
}

} // namespace revenant::volume
