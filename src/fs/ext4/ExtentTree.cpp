// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ext4/ExtentTree.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fs/SlotReader.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ext4 {

namespace {

constexpr std::uint16_t kMagic = 0xF30A;

constexpr std::size_t kMagicOffset = 0x00;
constexpr std::size_t kEntryCountOffset = 0x02;
constexpr std::size_t kMaxEntriesOffset = 0x04;
constexpr std::size_t kDepthOffset = 0x06;

// A leaf entry's fields, and an index entry's.
constexpr std::size_t kFileBlockOffset = 0x00;
constexpr std::size_t kLengthOffset = 0x04;
constexpr std::size_t kStartHighOffset = 0x06;
constexpr std::size_t kStartLowOffset = 0x08;
constexpr std::size_t kLeafLowOffset = 0x04;
constexpr std::size_t kLeafHighOffset = 0x08;

// ext4 marks an extent unwritten by adding this to its length, so a length above
// it is a length *and* a flag.
constexpr std::uint32_t kMaxInitializedLength = 32768;

[[nodiscard]] Result<ExtentNode> rejectAt(std::size_t offset) {
	return Error{.code = ErrorCode::kInvalidArgument, .offset = offset};
}

// What a node claims to hold has to fit in the node that holds it.
[[nodiscard]] Result<ExtentNode> checkedNode(const ExtentNode& node, std::size_t nodeBytes) {
	if (node.depth > kMaxExtentDepth) {
		return rejectAt(kDepthOffset);
	}
	if (kExtentHeaderBytes + (std::size_t{node.entries} * kExtentEntryBytes) > nodeBytes) {
		return rejectAt(kEntryCountOffset);
	}
	return node;
}

[[nodiscard]] Result<ExtentNode> headerOf(const ByteReader& reader, std::size_t nodeBytes) {
	if (slotFieldAt<std::uint16_t>(reader, kMagicOffset) != kMagic) {
		return rejectAt(kMagicOffset);
	}
	return checkedNode(
		ExtentNode{
			.entries = slotFieldAt<std::uint16_t>(reader, kEntryCountOffset),
			.max = slotFieldAt<std::uint16_t>(reader, kMaxEntriesOffset),
			.depth = slotFieldAt<std::uint16_t>(reader, kDepthOffset)},
		nodeBytes);
}

// The header of a node that must be of a given kind. A leaf read as an interior
// node — or the other way round — would turn one kind of block number into the
// other, and both address something.
[[nodiscard]] Result<ExtentNode> nodeOfDepth(std::span<const std::byte> node, bool wantLeaf) {
	return parseExtentHeader(node).andThen([wantLeaf](const ExtentNode& header) {
		return (header.depth == 0) == wantLeaf ? Result<ExtentNode>(header)
											   : rejectAt(kDepthOffset);
	});
}

[[nodiscard]] std::span<const std::byte>
entryAt(std::span<const std::byte> node, std::uint16_t at) {
	return node.subspan(
		kExtentHeaderBytes + (std::size_t{at} * kExtentEntryBytes),
		kExtentEntryBytes);
}

[[nodiscard]] Ext4Extent leafOf(const ByteReader& reader) {
	const std::uint32_t length = slotFieldAt<std::uint16_t>(reader, kLengthOffset);
	const bool initialized = length <= kMaxInitializedLength;
	const std::uint64_t high = slotFieldAt<std::uint16_t>(reader, kStartHighOffset);
	return Ext4Extent{
		.firstFileBlock = slotFieldAt<std::uint32_t>(reader, kFileBlockOffset),
		.blockCount = initialized ? length : length - kMaxInitializedLength,
		.firstDeviceBlock = (high << 32U) | slotFieldAt<std::uint32_t>(reader, kStartLowOffset),
		.initialized = initialized};
}

[[nodiscard]] ExtentIndex indexOf(const ByteReader& reader) {
	const std::uint64_t high = slotFieldAt<std::uint16_t>(reader, kLeafHighOffset);
	return ExtentIndex{
		.firstFileBlock = slotFieldAt<std::uint32_t>(reader, kFileBlockOffset),
		.nodeBlock = (high << 32U) | slotFieldAt<std::uint32_t>(reader, kLeafLowOffset)};
}

// Every entry of `node`, read by `readOne`. The header's bounds check is what
// makes each of these reads safe.
template <typename Entry, typename ReadOne>
[[nodiscard]] std::vector<Entry>
entriesOf(std::span<const std::byte> node, std::uint16_t count, ReadOne readOne) {
	std::vector<Entry> entries;
	entries.reserve(count);
	for (std::uint16_t at = 0; at < count; ++at) {
		entries.push_back(readOne(ByteReader{entryAt(node, at)}));
	}
	return entries;
}

} // namespace

Result<ExtentNode> parseExtentHeader(std::span<const std::byte> node) {
	return slotReader(node, kExtentHeaderBytes).andThen([&node](const ByteReader& reader) {
		return headerOf(reader, node.size());
	});
}

Result<std::vector<Ext4Extent>> parseExtentLeaves(std::span<const std::byte> node) {
	return nodeOfDepth(node, true).map([&node](const ExtentNode& header) {
		return entriesOf<Ext4Extent>(node, header.entries, leafOf);
	});
}

Result<std::vector<ExtentIndex>> parseExtentIndices(std::span<const std::byte> node) {
	return nodeOfDepth(node, false).map([&node](const ExtentNode& header) {
		return entriesOf<ExtentIndex>(node, header.entries, indexOf);
	});
}

} // namespace revenant::fs::ext4
