// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ClusterChain.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs {

namespace {

constexpr std::size_t kEntryBytes = 4;

[[nodiscard]] Result<std::uint32_t> decodeEntry(std::span<const std::byte, kEntryBytes> raw) {
	return fromLittleEndian<std::uint32_t>(raw) & kClusterMask;
}

// A chain ends at an end-of-chain marker and nowhere else. Running into a free
// entry or a bad cluster means the chain no longer describes a file — which is
// exactly what deletion leaves behind — so it is a rejection, not a short read.
[[nodiscard]] Result<std::vector<std::uint32_t>>
chainEnd(const Result<std::uint32_t>& last, std::vector<std::uint32_t>& clusters) {
	if (!last.hasValue()) {
		return last.error();
	}
	if (last.value() < kEndOfChain) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = last.value()};
	}
	return std::move(clusters);
}

// Whether `cluster` continues the extent that ends at `extent`.
[[nodiscard]] bool
follows(const Extent& extent, const ClusterGeometry& geometry, std::uint32_t cluster) {
	return clusterOffset(geometry, cluster) == extent.deviceOffset + extent.lengthBytes;
}

void appendCluster(
	std::vector<Extent>& extents,
	const ClusterGeometry& geometry,
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

ClusterChain::ClusterChain(BlockDevice& device, const ClusterGeometry& geometry) noexcept
	: device_(&device), geometry_(geometry) {}

const ClusterGeometry& ClusterChain::geometry() const noexcept {
	return geometry_;
}

bool ClusterChain::isDataCluster(std::uint32_t value) const noexcept {
	return value >= kFirstCluster && value < kFirstCluster + geometry_.totalClusters;
}

Result<std::size_t> ClusterChain::read(std::uint64_t offset, std::span<std::byte> buffer) const {
	const auto got = device_->readAt(offset, buffer);
	if (got.hasValue() && got.value() != buffer.size()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = offset};
	}
	return got;
}

Result<std::uint32_t> ClusterChain::entryAt(std::uint32_t cluster) const {
	const auto offset = geometry_.tableOffsetBytes + (std::uint64_t{cluster} * kEntryBytes);
	if (offset + kEntryBytes > geometry_.tableOffsetBytes + geometry_.tableSizeBytes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = offset};
	}
	std::array<std::byte, kEntryBytes> raw{};
	return read(offset, raw).andThen([&raw](std::size_t) { return decodeEntry(raw); });
}

Result<std::vector<std::uint32_t>> ClusterChain::chainFrom(std::uint32_t first) const {
	std::vector<std::uint32_t> clusters;
	Result<std::uint32_t> cluster = first;
	while (cluster.hasValue() && isDataCluster(cluster.value())) {
		if (clusters.size() >= geometry_.totalClusters) {
			return Error{.code = ErrorCode::kOutOfRange, .offset = clusters.size()};
		}
		clusters.push_back(cluster.value());
		cluster = entryAt(cluster.value());
	}
	return chainEnd(cluster, clusters);
}

std::uint64_t clusterOffset(const ClusterGeometry& geometry, std::uint32_t cluster) {
	const auto index = std::uint64_t{cluster} - kFirstCluster;
	return geometry.dataOffsetBytes + (index * geometry.bytesPerCluster);
}

Result<std::vector<Extent>> chainExtents(
	std::span<const std::uint32_t> clusters,
	const ClusterGeometry& geometry,
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
contiguousExtents(std::uint32_t first, const ClusterGeometry& geometry, std::uint64_t sizeBytes) {
	if (sizeBytes == 0) {
		return std::vector<Extent>{};
	}
	const auto clusters = ((sizeBytes - 1) / geometry.bytesPerCluster) + 1;
	if (std::uint64_t{first} + clusters > kFirstCluster + geometry.totalClusters) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = first};
	}
	return std::vector<Extent>{
		Extent{.deviceOffset = clusterOffset(geometry, first), .lengthBytes = sizeBytes}};
}

} // namespace revenant::fs
