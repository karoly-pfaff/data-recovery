// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>

#include "BootRegionInternal.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/SafeArith.hpp"
#include "revenant/fs/exfat/BootRegion.hpp"

namespace revenant::fs::exfat {

namespace {

// The three byte offsets the geometry is stated in, all derived from the same
// sector size.
struct ByteLayout {
	std::uint64_t fatOffset;
	std::uint64_t fatSize;
	std::uint64_t heapOffset;
};

// The cluster heap has to begin inside the volume, and there has to be at least
// one cluster in it, before anything else is worth deriving.
[[nodiscard]] Result<bool> heapFits(const BootRegion& region) {
	if (region.clusterHeapSector >= region.volumeSectors) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kClusterHeapOffsetOffset};
	}
	if (region.clusterCount == 0) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kClusterCountOffset};
	}
	return true;
}

// The root directory has to start at a cluster the heap actually holds.
[[nodiscard]] Result<std::uint32_t> checkedRoot(const BootRegion& region) {
	const auto root = region.rootCluster;
	if (root < kFirstDataCluster || root >= region.clusterCount + kFirstDataCluster) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kRootClusterOffset};
	}
	return root;
}

[[nodiscard]] Result<ByteLayout> layoutAfterFat(const BootRegion& region, std::uint64_t fatOffset) {
	return safeMul64(region.fatSectors, region.bytesPerSector, kFatLengthOffset)
		.andThen([&](std::uint64_t fatSize) {
			return safeMul64(
					   region.clusterHeapSector,
					   region.bytesPerSector,
					   kClusterHeapOffsetOffset)
				.map([&](std::uint64_t heapOffset) {
					return ByteLayout{
						.fatOffset = fatOffset,
						.fatSize = fatSize,
						.heapOffset = heapOffset};
				});
		});
}

[[nodiscard]] Result<ByteLayout> byteLayout(const BootRegion& region) {
	return safeMul64(region.fatSector, region.bytesPerSector, kFatOffsetOffset)
		.andThen([&](std::uint64_t fatOffset) { return layoutAfterFat(region, fatOffset); });
}

[[nodiscard]] ExfatGeometry assemble(
	const BootRegion& region,
	const ByteLayout& layout,
	std::uint32_t clusterBytes,
	std::uint32_t root) {
	return ExfatGeometry{
		.bytesPerSector = region.bytesPerSector,
		.bytesPerCluster = clusterBytes,
		.fatCount = region.fatCount,
		.fatOffsetBytes = layout.fatOffset,
		.fatSizeBytes = layout.fatSize,
		.clusterHeapOffsetBytes = layout.heapOffset,
		.totalClusters = region.clusterCount,
		.rootCluster = root};
}

[[nodiscard]] Result<ExfatGeometry> geometryAt(const BootRegion& region, std::uint32_t root) {
	return byteLayout(region).andThen([&](const ByteLayout& layout) {
		return safeMul32(region.bytesPerSector, region.sectorsPerCluster, kClusterShiftOffset)
			.map([&](std::uint32_t clusterBytes) {
				return assemble(region, layout, clusterBytes, root);
			});
	});
}

} // namespace

Result<ExfatGeometry> geometryOf(const BootRegion& region) {
	return heapFits(region)
		.andThen([&](bool) { return checkedRoot(region); })
		.andThen([&](std::uint32_t root) { return geometryAt(region, root); });
}

} // namespace revenant::fs::exfat
