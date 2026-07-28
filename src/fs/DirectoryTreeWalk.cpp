// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/DirectoryTreeWalk.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fs/ClusterChain.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs {

namespace {

[[nodiscard]] Result<std::size_t>
appendCluster(std::vector<std::byte>& bytes, const ClusterChain& chain, std::uint32_t cluster) {
	const auto clusterBytes = chain.geometry().bytesPerCluster;
	const auto at = bytes.size();
	bytes.resize(at + clusterBytes, std::byte{0});
	return chain.read(clusterOffset(chain.geometry(), cluster), std::span{bytes}.subspan(at));
}

} // namespace

Result<std::uint64_t> skipUnreadableDirectory(const Error& error) {
	if (error.code == ErrorCode::kIoFailure) {
		return error;
	}
	return std::uint64_t{0};
}

Result<std::vector<std::byte>> readDirectoryBytes(
	const ClusterChain& chain,
	std::span<const std::uint32_t> clusters,
	std::size_t capBytes) {
	std::vector<std::byte> bytes;
	for (auto at = clusters.begin(); at != clusters.end() && bytes.size() < capBytes; ++at) {
		const auto read = appendCluster(bytes, chain, *at);
		if (!read.hasValue()) {
			return read.error();
		}
	}
	return bytes;
}

} // namespace revenant::fs
