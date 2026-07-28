// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/fat/DirectoryBytes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fs/ClusterChain.hpp"
#include "fs/fat/DirectoryWalk.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/fat/DirectoryEntry.hpp"

namespace revenant::fs::fat {

namespace {

// Appends one cluster's worth of the directory. A short read means the volume
// ends inside the directory, which makes the rest of it unreadable rather than
// empty.
[[nodiscard]] Result<std::size_t>
appendCluster(std::vector<std::byte>& bytes, const ClusterChain& table, std::uint32_t cluster) {
	const auto clusterBytes = table.geometry().bytesPerCluster;
	const auto at = bytes.size();
	bytes.resize(at + clusterBytes, std::byte{0});
	return table.read(clusterOffset(table.geometry(), cluster), std::span{bytes}.subspan(at));
}

// A directory bigger than the cap is read up to it and no further: what a
// volume claims about its own size is data like any other (ADR-0009).
[[nodiscard]] bool roomFor(const std::vector<std::byte>& bytes) {
	return bytes.size() < kMaxDirectoryBytes;
}

[[nodiscard]] Result<std::vector<std::byte>>
readClusters(const ClusterChain& table, std::span<const std::uint32_t> clusters) {
	std::vector<std::byte> bytes;
	for (auto cluster = clusters.begin(); cluster != clusters.end() && roomFor(bytes); ++cluster) {
		const auto read = appendCluster(bytes, table, *cluster);
		if (!read.hasValue()) {
			return read.error();
		}
	}
	return bytes;
}

} // namespace

Result<std::vector<std::byte>>
readDirectory(const ClusterChain& table, std::uint32_t cluster, bool freedChain) {
	if (!table.isDataCluster(cluster)) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = cluster};
	}
	if (freedChain) {
		const std::array<std::uint32_t, 1> only{cluster};
		return readClusters(table, only);
	}
	return table.chainFrom(cluster).andThen([&table](const std::vector<std::uint32_t>& clusters) {
		return readClusters(table, clusters);
	});
}

std::span<const std::byte> upToEndOfDirectory(std::span<const std::byte> bytes) {
	for (std::size_t at = 0; at + kDirectoryEntryBytes <= bytes.size();
		 at += kDirectoryEntryBytes) {
		const auto kind = classifyEntry(bytes.subspan(at, kDirectoryEntryBytes));
		if (kind.hasValue() && kind.value() == EntryKind::kEndOfDirectory) {
			return bytes.first(at);
		}
	}
	return bytes;
}

} // namespace revenant::fs::fat
