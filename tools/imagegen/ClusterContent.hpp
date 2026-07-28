// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Spreading a fixture file's content across the clusters a layout gave it.
// FAT32 and exFAT lay their data regions out the same way — fixed-size clusters
// addressed from 2 — so this is written once. Each builder still says *where*
// its clusters are; only the spreading is shared.
//
// Every function here is `inline` or a template, so the header is safely
// includable from any translation unit.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "imagegen/ByteWriter.hpp"

namespace revenant::imagegen {

// One cluster's share of `content`, or nothing if the file ran out before it.
[[nodiscard]] inline std::span<const std::byte>
clusterShare(std::span<const std::byte> content, std::size_t from, std::size_t clusterBytes) {
	if (from >= content.size()) {
		return {};
	}
	return content.subspan(from, std::min<std::size_t>(clusterBytes, content.size() - from));
}

// One table entry as a builder states it: which cluster, and where it points.
struct ChainLink {
	std::uint32_t cluster;
	std::uint32_t next;
};

// Chains `clusters` together in the table, ending the last one. Where an entry
// lives is the caller's business — `putLink` is asked — but that a chain is a
// list of links terminated by an end-of-chain marker is the same in both
// filesystems.
template <typename PutLink>
void putClusterChain(
	const std::vector<std::uint32_t>& clusters,
	std::uint32_t endOfChain,
	PutLink putLink) {
	for (std::size_t at = 0; at + 1 < clusters.size(); ++at) {
		putLink(ChainLink{.cluster = clusters.at(at), .next = clusters.at(at + 1)});
	}
	putLink(ChainLink{.cluster = clusters.back(), .next = endOfChain});
}

// Writes `content` into `image` across `clusters`, asking `offsetOf` where each
// one begins on the device.
template <typename OffsetOf>
void putClusteredContent(
	std::vector<std::byte>& image,
	std::span<const std::byte> content,
	const std::vector<std::uint32_t>& clusters,
	std::size_t clusterBytes,
	OffsetOf offsetOf) {
	for (std::size_t at = 0; at < clusters.size(); ++at) {
		const auto share = clusterShare(content, at * clusterBytes, clusterBytes);
		putBytes(image, static_cast<std::size_t>(offsetOf(clusters.at(at))), share);
	}
}

} // namespace revenant::imagegen
