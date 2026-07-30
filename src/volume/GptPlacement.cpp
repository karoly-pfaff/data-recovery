// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"
#include "core/SafeArith.hpp"
#include "revenant/volume/Gpt.hpp"
#include "revenant/volume/GptPartitions.hpp"
#include "volume/GptInternal.hpp"

namespace revenant::volume {

namespace {

// An entry states an inclusive last sector, so it spans one more sector than the
// difference. Both steps are checked: an entry claiming the last addressable
// sector would otherwise wrap the count to zero and report a partition of no
// length where it means one of the whole disk.
[[nodiscard]] Result<std::uint64_t> lengthBytesOf(const GptEntry& entry, std::uint32_t sectorSize) {
	return safeAdd64(entry.lastLba - entry.firstLba, 1, /*offset=*/0)
		.andThen([sectorSize](std::uint64_t sectors) {
			return safeMul64(sectors, sectorSize, /*offset=*/0);
		});
}

[[nodiscard]] Result<GptPartition> partitionOf(const GptEntry& entry, std::uint32_t sectorSize) {
	return safeMul64(entry.firstLba, sectorSize, /*offset=*/0)
		.andThen([&entry, sectorSize](std::uint64_t startBytes) {
			return lengthBytesOf(entry, sectorSize).map([&](std::uint64_t lengthBytes) {
				return GptPartition{
					.startBytes = startBytes,
					.lengthBytes = lengthBytes,
					.typeGuid = entry.typeGuid,
					.name = entry.name,
					.nameIsExact = entry.nameIsExact};
			});
		});
}

// One slot folded into the list: an unused slot contributes nothing, a used one
// its byte range.
[[nodiscard]] Result<std::vector<GptPartition>> appendEntry(
	std::span<const std::byte> slot,
	std::vector<GptPartition> found,
	std::uint32_t sectorSize) {
	return parseGptEntry(slot).andThen([&](const GptEntry& entry) {
		if (isUnusedEntry(entry)) {
			return Result<std::vector<GptPartition>>(found);
		}
		return partitionOf(entry, sectorSize).map([&](const GptPartition& partition) {
			found.push_back(partition);
			return found;
		});
	});
}

// Slot `index`'s bytes. The array was read at exactly entryCount * entryBytes,
// and `index` is below entryCount, so the window is always inside it.
[[nodiscard]] std::span<const std::byte>
slotAt(std::span<const std::byte> array, const GptHeader& header, std::uint32_t index) {
	return array.subspan(static_cast<std::size_t>(index) * header.entryBytes, header.entryBytes);
}

} // namespace

Result<std::vector<GptPartition>>
partitionsIn(std::span<const std::byte> array, const GptHeader& header, std::uint32_t sectorSize) {
	Result<std::vector<GptPartition>> found{std::vector<GptPartition>{}};
	for (std::uint32_t index = 0; index < header.entryCount; ++index) {
		found = found.andThen([&](const std::vector<GptPartition>& built) {
			return appendEntry(slotAt(array, header, index), built, sectorSize);
		});
	}
	return found;
}

} // namespace revenant::volume
