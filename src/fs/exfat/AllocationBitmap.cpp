// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/exfat/AllocationBitmap.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "fs/ClusterChain.hpp"
#include "fs/DirectoryTreeWalk.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/exfat/DirectoryEntry.hpp"

namespace revenant::fs::exfat {

namespace {

// Where the bitmap entry keeps the two things needed to read it.
constexpr std::size_t kBitmapClusterOffset = 0x14;
constexpr std::size_t kBitmapLengthOffset = 0x18;

// The bitmap entry a directory holds, if it holds one.
struct BitmapLocation {
	std::uint32_t firstCluster;
	std::uint64_t lengthBytes;
};

[[nodiscard]] std::optional<BitmapLocation> locationIn(std::span<const std::byte> slot) {
	const ByteReader reader{slot};
	const auto cluster = reader.readLe<std::uint32_t>(kBitmapClusterOffset);
	const auto length = reader.readLe<std::uint64_t>(kBitmapLengthOffset);
	if (!cluster.hasValue() || !length.hasValue()) {
		return std::nullopt;
	}
	return BitmapLocation{.firstCluster = cluster.value(), .lengthBytes = length.value()};
}

// The first allocation-bitmap entry the directory names. exFAT allows a second
// for its TexFAT variant; this build reads the first, which is the one that
// describes the volume a non-TexFAT driver sees.
[[nodiscard]] std::optional<BitmapLocation> findBitmap(std::span<const std::byte> bytes) {
	for (std::size_t at = 0; at + kDirectoryEntryBytes <= bytes.size();
		 at += kDirectoryEntryBytes) {
		const auto slot = bytes.subspan(at, kDirectoryEntryBytes);
		const auto header = classifyExfatEntry(slot);
		if (header.hasValue() && header.value().kind == ExfatEntryKind::kAllocationBitmap) {
			return locationIn(slot);
		}
	}
	return std::nullopt;
}

[[nodiscard]] std::vector<std::byte>
readBits(const ClusterChain& chain, const BitmapLocation& where) {
	const auto clusters = chain.chainFrom(where.firstCluster);
	if (!clusters.hasValue() || where.lengthBytes > kMaxBitmapBytes) {
		return {};
	}
	auto bytes = readDirectoryBytes(chain, clusters.value(), kMaxBitmapBytes);
	if (!bytes.hasValue()) {
		return {};
	}
	bytes.value().resize(std::min<std::size_t>(bytes.value().size(), where.lengthBytes));
	return std::move(bytes.value());
}

} // namespace

AllocationBitmap::AllocationBitmap(std::vector<std::byte> bits) noexcept : bits_(std::move(bits)) {}

bool AllocationBitmap::known() const noexcept {
	return !bits_.empty();
}

bool AllocationBitmap::isAllocated(std::uint32_t cluster) const noexcept {
	if (cluster < kFirstCluster) {
		return true;
	}
	const std::size_t index = cluster - kFirstCluster;
	if (index / 8 >= bits_.size()) {
		return true;
	}
	const auto byte = std::to_integer<unsigned>(bits_.at(index / 8));
	return (byte & (1U << (index % 8))) != 0;
}

AllocationBitmap
readAllocationBitmap(const ClusterChain& chain, std::span<const std::byte> directoryBytes) {
	const auto where = findBitmap(directoryBytes);
	if (!where.has_value()) {
		return AllocationBitmap{};
	}
	return AllocationBitmap{readBits(chain, *where)};
}

} // namespace revenant::fs::exfat
