// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/volume/GptPartitions.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/volume/Gpt.hpp"
#include "volume/GptInternal.hpp"
#include "volume/SectorIo.hpp"

namespace revenant::volume {

namespace {

// The last addressable sector, where the backup header lives. A device with no
// room for even one sector has no last one either.
[[nodiscard]] Result<std::uint64_t> lastLbaOf(const BlockDevice& device) {
	const std::uint64_t sectorSize = device.sectorSize();
	if (sectorSize == 0 || device.sizeInBytes() < sectorSize) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = 0};
	}
	return (device.sizeInBytes() / sectorSize) - 1;
}

[[nodiscard]] Result<std::vector<GptPartition>>
partitionsFor(BlockDevice& device, const GptHeader& header) {
	return readEntryArray(device, header).andThen([&](const std::vector<std::byte>& array) {
		return partitionsIn(array, header, device.sectorSize());
	});
}

// One whole copy of the table: the header at `lba` and the array it vouches for.
// They succeed or fail together, because an array that fails its own checksum is
// exactly as unusable as a header that fails its.
[[nodiscard]] Result<std::vector<GptPartition>> tableAt(BlockDevice& device, std::uint64_t lba) {
	return readTableSector(device, lba)
		.andThen([lba](const TableSector& sector) { return parseGptHeader(sector, lba); })
		.andThen([&device](const GptHeader& header) { return partitionsFor(device, header); });
}

[[nodiscard]] Result<std::vector<GptPartition>> backupTable(BlockDevice& device) {
	return lastLbaOf(device).andThen([&device](std::uint64_t lba) { return tableAt(device, lba); });
}

} // namespace

Result<GptDisk> readGptPartitions(BlockDevice& device) {
	const auto primary = tableAt(device, kPrimaryHeaderLba);
	if (primary.hasValue()) {
		return GptDisk{.partitions = primary.value(), .fromBackupHeader = false};
	}
	const auto backup = backupTable(device);
	if (!backup.hasValue()) {
		return primary.error();
	}
	return GptDisk{.partitions = backup.value(), .fromBackupHeader = true};
}

} // namespace revenant::volume
