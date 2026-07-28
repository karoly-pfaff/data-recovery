// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery/VolumeWalk.hpp"

#include <cstddef>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/EntryEnumeration.hpp"
#include "revenant/fs/ntfs/MftTable.hpp"

namespace revenant::recovery {

namespace {

constexpr std::size_t kBootSectorBytes = 512;

[[nodiscard]] Result<fs::ntfs::NtfsGeometry> readGeometry(BlockDevice& device) {
	std::vector<std::byte> sector(kBootSectorBytes, std::byte{0});
	const auto read = device.readAt(0, sector);
	if (!read.hasValue()) {
		return read.error();
	}
	if (read.value() != sector.size()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = 0};
	}
	return fs::ntfs::parseBootSector(sector);
}

} // namespace

Result<fs::ntfs::EnumerationStats> enumerateVolume(BlockDevice& device, fs::EntryVisitor& visitor) {
	return readGeometry(device)
		.andThen([&device](const fs::ntfs::NtfsGeometry& geometry) {
			return fs::ntfs::MftTable::open(device, geometry);
		})
		.andThen([&visitor](const fs::ntfs::MftTable& table) {
			return fs::ntfs::enumerateEntries(table, visitor);
		});
}

} // namespace revenant::recovery
