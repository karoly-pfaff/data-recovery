// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/exfat/ExfatFileSystem.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "BootRegionInternal.hpp"
#include "fs/ClusterChain.hpp"
#include "fs/MountRegion.hpp"
#include "fs/exfat/EntrySetWalk.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/exfat/BootRegion.hpp"

namespace revenant::fs::exfat {

namespace {

// exFAT's geometry restated in the terms a chain follower needs. What it leaves
// out — where the root directory starts — the walk is told separately.
[[nodiscard]] ClusterGeometry chainGeometryOf(const ExfatGeometry& geometry) {
	return ClusterGeometry{
		.bytesPerCluster = geometry.bytesPerCluster,
		.tableOffsetBytes = geometry.fatOffsetBytes,
		.tableSizeBytes = geometry.fatSizeBytes,
		.dataOffsetBytes = geometry.clusterHeapOffsetBytes,
		.totalClusters = geometry.totalClusters};
}

class ExfatFileSystem final : public FileSystem {
public:
	ExfatFileSystem(BlockDevice& device, const ExfatGeometry& geometry) noexcept
		: chain_(device, chainGeometryOf(geometry)), rootCluster_(geometry.rootCluster) {}

	[[nodiscard]] Result<EnumerationStats> enumerate(EntryVisitor& visitor) const override {
		return walkVolume(chain_, rootCluster_, visitor);
	}

private:
	ClusterChain chain_;
	std::uint32_t rootCluster_;
};

// exFAT names itself, and zeroes the field a FAT BPB keeps its geometry in.
// Anything else is another filesystem's volume rather than a broken exFAT one,
// and is handed back to the mount table to keep looking.
[[nodiscard]] Result<ExfatGeometry> recognize(std::span<const std::byte> sector) {
	if (!namesExfat(ByteReader{sector}).hasValue()) {
		return Error{.code = ErrorCode::kNotFound};
	}
	return parseExfatBootSector(sector);
}

} // namespace

Result<std::unique_ptr<FileSystem>> mountExfat(BlockDevice& device) {
	return readMountRegion(device, MountRegion{.offset = 0, .length = kBootSectorBytes})
		.andThen(recognize)
		.andThen([&device](const ExfatGeometry& geometry) -> Result<std::unique_ptr<FileSystem>> {
			return std::unique_ptr<FileSystem>{std::make_unique<ExfatFileSystem>(device, geometry)};
		});
}

} // namespace revenant::fs::exfat
