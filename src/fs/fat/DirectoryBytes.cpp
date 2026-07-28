// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/fat/DirectoryBytes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fs/ClusterChain.hpp"
#include "fs/DirectoryTreeWalk.hpp"
#include "fs/fat/DirectoryWalk.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/fat/DirectoryEntry.hpp"

namespace revenant::fs::fat {

namespace {} // namespace

Result<std::vector<std::byte>>
readDirectory(const ClusterChain& table, std::uint32_t cluster, bool freedChain) {
	if (!table.isDataCluster(cluster)) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = cluster};
	}
	if (freedChain) {
		const std::array<std::uint32_t, 1> only{cluster};
		return readDirectoryBytes(table, only, kMaxDirectoryBytes);
	}
	return table.chainFrom(cluster).andThen([&table](const std::vector<std::uint32_t>& clusters) {
		return readDirectoryBytes(table, clusters, kMaxDirectoryBytes);
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
