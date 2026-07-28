// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/fat/ChainExtents.hpp"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/fat/BootSector.hpp"

namespace revenant::fs::fat {

namespace {

// Whether `cluster` continues the extent that ends at `extent`.
[[nodiscard]] bool
follows(const Extent& extent, const Fat32Geometry& geometry, std::uint32_t cluster) {
	return clusterOffset(geometry, cluster) == extent.deviceOffset + extent.lengthBytes;
}

void appendCluster(
	std::vector<Extent>& extents,
	const Fat32Geometry& geometry,
	std::uint32_t cluster) {
	if (!extents.empty() && follows(extents.back(), geometry, cluster)) {
		extents.back().lengthBytes += geometry.bytesPerCluster;
		return;
	}
	extents.push_back(
		Extent{
			.deviceOffset = clusterOffset(geometry, cluster),
			.lengthBytes = geometry.bytesPerCluster});
}

[[nodiscard]] std::uint64_t totalOf(const std::vector<Extent>& extents) {
	std::uint64_t total = 0;
	for (const Extent& extent : extents) {
		total += extent.lengthBytes;
	}
	return total;
}

// Cuts the tail so the extents describe exactly `sizeBytes`. A file never fills
// its last cluster exactly, and handing back the slack would append whatever
// the volume happened to leave there.
[[nodiscard]] Result<std::vector<Extent>>
trimmedTo(std::vector<Extent> extents, std::uint64_t sizeBytes) {
	const auto allocated = totalOf(extents);
	if (sizeBytes > allocated) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = sizeBytes};
	}
	extents.back().lengthBytes -= allocated - sizeBytes;
	return extents;
}

} // namespace

std::uint64_t clusterOffset(const Fat32Geometry& geometry, std::uint32_t cluster) {
	const auto index = std::uint64_t{cluster} - kFirstDataCluster;
	return geometry.dataOffsetBytes + (index * geometry.bytesPerCluster);
}

Result<std::vector<Extent>> chainExtents(
	std::span<const std::uint32_t> clusters,
	const Fat32Geometry& geometry,
	std::uint64_t sizeBytes) {
	if (clusters.empty() || sizeBytes == 0) {
		return std::vector<Extent>{};
	}
	std::vector<Extent> extents;
	for (const std::uint32_t cluster : clusters) {
		appendCluster(extents, geometry, cluster);
	}
	return trimmedTo(std::move(extents), sizeBytes);
}

Result<std::vector<Extent>>
contiguousExtents(std::uint32_t first, const Fat32Geometry& geometry, std::uint64_t sizeBytes) {
	if (sizeBytes == 0) {
		return std::vector<Extent>{};
	}
	const auto clusters = ((sizeBytes - 1) / geometry.bytesPerCluster) + 1;
	if (std::uint64_t{first} + clusters > kFirstDataCluster + geometry.totalClusters) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = first};
	}
	return std::vector<Extent>{
		Extent{.deviceOffset = clusterOffset(geometry, first), .lengthBytes = sizeBytes}};
}

} // namespace revenant::fs::fat
