// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ext4/BlockReader.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ext4/Superblock.hpp"

namespace revenant::fs::ext4 {

Ext4Blocks::Ext4Blocks(BlockDevice& device, const Ext4Geometry& geometry) noexcept
	: reader_(device), geometry_(geometry) {}

const Ext4Geometry& Ext4Blocks::geometry() const noexcept {
	return geometry_;
}

bool Ext4Blocks::isDataBlock(std::uint64_t block) const noexcept {
	return block >= geometry_.firstDataBlock && block < geometry_.totalBlocks;
}

std::uint64_t Ext4Blocks::blockOffset(std::uint64_t block) const noexcept {
	return block * geometry_.blockSizeBytes;
}

Result<std::size_t> Ext4Blocks::read(std::uint64_t offset, std::span<std::byte> buffer) const {
	return reader_.read(offset, buffer);
}

Result<std::vector<std::byte>> Ext4Blocks::readBlock(std::uint64_t block) const {
	if (!isDataBlock(block)) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = block};
	}
	std::vector<std::byte> bytes(geometry_.blockSizeBytes, std::byte{0});
	return read(blockOffset(block), bytes).map([&bytes](std::size_t) { return bytes; });
}

namespace {

// As much of `extent` as `capBytes` still leaves room for, appended. Zero when
// the cap is already reached, which ends the read.
[[nodiscard]] Result<std::size_t> appendExtentBytes(
	std::vector<std::byte>& bytes,
	const Ext4Blocks& blocks,
	const Extent& extent,
	std::size_t capBytes) {
	const auto at = bytes.size();
	const auto wanted = std::min<std::uint64_t>(extent.lengthBytes, capBytes - at);
	bytes.resize(at + static_cast<std::size_t>(wanted), std::byte{0});
	return blocks.read(extent.deviceOffset, std::span{bytes}.subspan(at));
}

} // namespace

// The cap bounds the *total*, one extent at a time. Checking it only between
// extents would let a single crafted run allocate gigabytes before anything
// looked at it — a file's stated size is data like any other (ADR-0009).
Result<std::vector<std::byte>>
readExtents(const Ext4Blocks& blocks, std::span<const Extent> extents, std::size_t capBytes) {
	std::vector<std::byte> bytes;
	for (auto at = extents.begin(); at != extents.end() && bytes.size() < capBytes; ++at) {
		const auto read = appendExtentBytes(bytes, blocks, *at, capBytes);
		if (!read.hasValue()) {
			return read.error();
		}
	}
	return bytes;
}

} // namespace revenant::fs::ext4
