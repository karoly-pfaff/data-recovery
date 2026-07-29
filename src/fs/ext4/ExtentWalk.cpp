// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ext4/ExtentWalk.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "fs/DirectoryTreeWalk.hpp"
#include "fs/ExtentSpan.hpp"
#include "fs/ext4/BlockReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ext4/ExtentTree.hpp"
#include "revenant/fs/ext4/Inode.hpp"

namespace revenant::fs::ext4 {

namespace {

// The tree, walked from an explicit worklist rather than by recursion: every
// block number it follows came off the disk.
class TreeWalk {
public:
	explicit TreeWalk(const Ext4Blocks& blocks) noexcept : blocks_(&blocks) {}

	[[nodiscard]] Result<std::vector<Ext4Extent>> run(std::vector<std::byte> root) {
		pending_.push_back(std::move(root));
		return driveWorklist(
				   pending_,
				   [this](const std::vector<std::byte>& node) { return walkNode(node); })
			.map([this](std::uint64_t) { return std::move(leaves_); });
	}

private:
	[[nodiscard]] Result<std::uint64_t> walkNode(std::span<const std::byte> node) {
		++visited_;
		if (visited_ > kMaxExtentNodes) {
			return Error{.code = ErrorCode::kOutOfRange, .offset = visited_};
		}
		return parseExtentHeader(node).andThen([&](const ExtentNode& header) {
			return header.depth == 0 ? takeLeaves(node) : followIndices(node);
		});
	}

	[[nodiscard]] Result<std::uint64_t> takeLeaves(std::span<const std::byte> node) {
		return parseExtentLeaves(node).andThen(
			[this](const std::vector<Ext4Extent>& found) -> Result<std::uint64_t> {
				if (leaves_.size() + found.size() > kMaxExtents) {
					return Error{.code = ErrorCode::kOutOfRange, .offset = leaves_.size()};
				}
				leaves_.insert(leaves_.end(), found.begin(), found.end());
				return static_cast<std::uint64_t>(found.size());
			});
	}

	[[nodiscard]] Result<std::uint64_t> followIndices(std::span<const std::byte> node) {
		return parseExtentIndices(node).andThen(
			[this](const std::vector<ExtentIndex>& indices) { return enqueue(indices); });
	}

	// A node's children are read as they are queued, so the budget is checked
	// here too: one wide interior node could otherwise pull its whole fan-out
	// into memory before a single child was looked at.
	[[nodiscard]] Result<std::uint64_t> enqueue(const std::vector<ExtentIndex>& indices) {
		for (const ExtentIndex& index : indices) {
			const auto queued = queueNode(index.nodeBlock);
			if (!queued.hasValue()) {
				return queued.error();
			}
		}
		return std::uint64_t{0};
	}

	[[nodiscard]] Result<std::uint64_t> queueNode(std::uint64_t block) {
		if (visited_ + pending_.size() >= kMaxExtentNodes) {
			return Error{.code = ErrorCode::kOutOfRange, .offset = pending_.size()};
		}
		auto node = blocks_->readBlock(block);
		if (!node.hasValue()) {
			return node.error();
		}
		pending_.push_back(std::move(node.value()));
		return std::uint64_t{0};
	}

	const Ext4Blocks* blocks_; // non-owning, never null
	std::vector<std::vector<std::byte>> pending_;
	std::vector<Ext4Extent> leaves_;
	std::size_t visited_ = 0;
};

// A leaf whose blocks the volume holds, that was actually written, and that
// covers something.
[[nodiscard]] bool usable(const Ext4Extent& leaf, const Ext4Blocks& blocks) {
	const auto last = leaf.firstDeviceBlock + leaf.blockCount - 1;
	return leaf.initialized && leaf.blockCount > 0 && blocks.isDataBlock(leaf.firstDeviceBlock) &&
		   blocks.isDataBlock(last);
}

[[nodiscard]] Extent deviceRun(const Ext4Extent& leaf, const Ext4Blocks& blocks) {
	return Extent{
		.deviceOffset = blocks.blockOffset(leaf.firstDeviceBlock),
		.lengthBytes = std::uint64_t{leaf.blockCount} * blocks.geometry().blockSizeBytes};
}

// The extents so far, and the file block the next leaf has to start at for them
// to stay a contiguous run.
struct Mapping {
	std::vector<Extent> extents;
	std::uint32_t wanted;
};

[[nodiscard]] bool takeLeaf(Mapping& mapping, const Ext4Extent& leaf, const Ext4Blocks& blocks) {
	if (leaf.firstFileBlock != mapping.wanted || !usable(leaf, blocks)) {
		return false;
	}
	appendExtent(mapping.extents, deviceRun(leaf, blocks));
	mapping.wanted += leaf.blockCount;
	return true;
}

// The leaves as device extents, in file order and with no gap between them. A
// tree's nodes are visited in whatever order the worklist reached them, so the
// leaves are sorted by where in the *file* they sit before anything is
// concluded from their adjacency.
[[nodiscard]] Result<std::vector<Extent>>
mapLeaves(std::vector<Ext4Extent> leaves, const Ext4Blocks& blocks) {
	std::ranges::sort(leaves, {}, &Ext4Extent::firstFileBlock);
	Mapping mapping{.extents = {}, .wanted = 0};
	for (const Ext4Extent& leaf : leaves) {
		if (!takeLeaf(mapping, leaf, blocks)) {
			return Error{.code = ErrorCode::kInvalidArgument, .offset = leaf.firstFileBlock};
		}
	}
	return mapping.extents;
}

} // namespace

Result<std::vector<Extent>>
treeExtents(const Ext4Blocks& blocks, std::span<const std::byte> root, std::uint64_t sizeBytes) {
	if (sizeBytes == 0) {
		return std::vector<Extent>{};
	}
	TreeWalk walk{blocks};
	return walk.run(std::vector<std::byte>{root.begin(), root.end()})
		.andThen(
			[&blocks](const std::vector<Ext4Extent>& leaves) { return mapLeaves(leaves, blocks); })
		.andThen([sizeBytes](const std::vector<Extent>& extents) {
			return trimToSize(extents, sizeBytes);
		});
}

Result<std::vector<Extent>> inodeExtents(const Ext4Blocks& blocks, const Ext4Inode& inode) {
	if (!inode.usesExtents) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = inode.flags};
	}
	return treeExtents(blocks, inode.blockMap, inode.sizeInBytes);
}

} // namespace revenant::fs::ext4
