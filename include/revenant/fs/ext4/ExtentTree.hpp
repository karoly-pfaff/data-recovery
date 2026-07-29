// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"

namespace revenant::fs::ext4 {

// Every node of an extent tree — the one in an inode's 60 bytes and the ones in
// blocks of their own — is a 12-byte header followed by 12-byte entries.
inline constexpr std::size_t kExtentHeaderBytes = 12;
inline constexpr std::size_t kExtentEntryBytes = 12;

// ext4 cannot build a tree deeper than this: five levels of 340-entry nodes
// address more blocks than a volume can hold. A node claiming more is a crafted
// volume trying to make a walk chase blocks, and is refused (ADR-0009).
inline constexpr std::uint16_t kMaxExtentDepth = 5;

// A node's header. `depth` is what says which kind of entries follow: zero for
// the extents themselves, anything above for indices pointing at further nodes.
struct ExtentNode {
	std::uint16_t entries;
	std::uint16_t max;
	std::uint16_t depth;
};

// One run of blocks a file occupies, in the file's own block numbering.
struct Ext4Extent {
	std::uint32_t firstFileBlock;
	std::uint32_t blockCount;
	std::uint64_t firstDeviceBlock;
	// ext4 can allocate blocks without writing them — a preallocated tail, or a
	// hole filled in advance. Those blocks hold whatever they last held, so a
	// walk that ignored this would pad a recovered file with someone else's
	// data.
	bool initialized;
};

// One entry of an interior node: where in the file its subtree starts, and
// which block holds that subtree.
struct ExtentIndex {
	std::uint32_t firstFileBlock;
	std::uint64_t nodeBlock;
};

// Reads a node's header.
//
// Input shorter than a header is `kOutOfRange`. A magic other than `0xF30A` is
// `kInvalidArgument` at `0x00`, a depth above five `kInvalidArgument` at `0x06`,
// and an entry count the node has no room for `kInvalidArgument` at `0x02` —
// what a node *claims* to hold is data like any other.
[[nodiscard]] Result<ExtentNode> parseExtentHeader(std::span<const std::byte> node);

// The extents of a leaf node, in the order it holds them. An interior node is
// `kInvalidArgument` at `0x06`: its entries are indices, and reading them as
// extents would produce block numbers that address nothing.
[[nodiscard]] Result<std::vector<Ext4Extent>> parseExtentLeaves(std::span<const std::byte> node);

// The indices of an interior node, likewise refusing a leaf.
[[nodiscard]] Result<std::vector<ExtentIndex>> parseExtentIndices(std::span<const std::byte> node);

} // namespace revenant::fs::ext4
