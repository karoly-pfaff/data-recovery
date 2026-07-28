// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/fat/FatTable.hpp"

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
#include "revenant/fs/fat/BootSector.hpp"

namespace revenant::fs::fat {

namespace {

constexpr std::size_t kFatEntryBytes = 4;

[[nodiscard]] Result<std::uint32_t> decodeEntry(std::span<const std::byte, kFatEntryBytes> raw) {
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

} // namespace

FatTable::FatTable(BlockDevice& device, const Fat32Geometry& geometry) noexcept
	: device_(&device), geometry_(geometry) {}

const Fat32Geometry& FatTable::geometry() const noexcept {
	return geometry_;
}

bool FatTable::isDataCluster(std::uint32_t value) const noexcept {
	return value >= kFirstDataCluster && value < kFirstDataCluster + geometry_.totalClusters;
}

Result<std::size_t> FatTable::read(std::uint64_t offset, std::span<std::byte> buffer) const {
	const auto got = device_->readAt(offset, buffer);
	if (got.hasValue() && got.value() != buffer.size()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = offset};
	}
	return got;
}

Result<std::uint32_t> FatTable::entryAt(std::uint32_t cluster) const {
	const auto offset = geometry_.fatOffsetBytes + (std::uint64_t{cluster} * kFatEntryBytes);
	if (offset + kFatEntryBytes > geometry_.fatOffsetBytes + geometry_.fatSizeBytes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = offset};
	}
	std::array<std::byte, kFatEntryBytes> raw{};
	return read(offset, raw).andThen([&raw](std::size_t) { return decodeEntry(raw); });
}

Result<std::vector<std::uint32_t>> FatTable::chainFrom(std::uint32_t first) const {
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

} // namespace revenant::fs::fat
