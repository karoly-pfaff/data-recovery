// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/io/CachingDevice.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/core/io/ReadRange.hpp"

namespace revenant {

namespace {

// A block must cover at least one sector — a shorter one would issue reads a raw
// device refuses — and must be a power of two, so that a byte offset divides
// into a block index without a remainder that has to be carried anywhere.
[[nodiscard]] std::uint64_t blockBytesFor(const CacheShape& shape, std::uint32_t sectorSize) {
	const auto floor = std::max<std::size_t>({shape.blockBytes, sectorSize, 1});
	return std::bit_ceil(static_cast<std::uint64_t>(floor));
}

} // namespace

CachingDevice::CachingDevice(BlockDevice& source, const CacheShape& shape)
	: source_(source), blockBytes_(blockBytesFor(shape, source.sectorSize())),
	  blockCount_(std::max<std::size_t>(shape.blockCount, 1)) {}

std::uint64_t CachingDevice::sizeInBytes() const {
	return source_.sizeInBytes();
}

std::uint32_t CachingDevice::sectorSize() const {
	return source_.sectorSize();
}

std::size_t CachingDevice::heldBlocks() const noexcept {
	return blocks_.size();
}

Result<std::size_t> CachingDevice::readAt(std::uint64_t offset, std::span<std::byte> buffer) {
	const auto want = clampReadRange(offset, buffer.size(), source_.sizeInBytes());
	if (!want.hasValue()) {
		return want.error();
	}
	return copyThroughCache(offset, buffer.first(want.value()));
}

// One more block folded into the copy. A block that supplies nothing is the end
// of the device, and `more` is what stops the walk where it stands rather than
// claiming the rest of the buffer.
Result<CachingDevice::CopyStep>
CachingDevice::advancedBy(std::uint64_t offset, std::span<std::byte> buffer, std::size_t done) {
	return copyOneBlock(offset + done, buffer.subspan(done)).map([done](std::size_t step) {
		return CopyStep{.done = done + step, .more = step != 0};
	});
}

Result<std::size_t>
CachingDevice::copyThroughCache(std::uint64_t offset, std::span<std::byte> buffer) {
	CopyStep step{.done = 0, .more = true};
	while (step.more && step.done < buffer.size()) {
		const auto next = advancedBy(offset, buffer, step.done);
		if (!next.hasValue()) {
			return next.error();
		}
		step = next.value();
	}
	return step.done;
}

// As much of `buffer` as the block holding `offset` can supply. Zero means that
// block held nothing past this point, which is the end of the device.
Result<std::size_t> CachingDevice::copyOneBlock(std::uint64_t offset, std::span<std::byte> buffer) {
	const auto within = static_cast<std::size_t>(offset % blockBytes_);
	return blockAt(offset / blockBytes_).map([&](const CachedBlock* block) {
		if (within >= block->bytes.size()) {
			return std::size_t{0};
		}
		const auto count = std::min(buffer.size(), block->bytes.size() - within);
		const auto from = block->bytes.begin() + static_cast<std::ptrdiff_t>(within);
		std::copy_n(from, count, buffer.begin());
		return count;
	});
}

Result<const CachingDevice::CachedBlock*> CachingDevice::blockAt(std::uint64_t index) {
	const auto found = held_.find(index);
	if (found == held_.end()) {
		return loadBlock(index);
	}
	blocks_.splice(blocks_.begin(), blocks_, found->second);
	found->second = blocks_.begin();
	return &blocks_.front();
}

// `index` came from an offset already inside the device, so the product below
// cannot exceed that offset and needs no overflow check.
Result<const CachingDevice::CachedBlock*> CachingDevice::loadBlock(std::uint64_t index) {
	std::vector<std::byte> bytes(static_cast<std::size_t>(blockBytes_));
	const auto read = source_.readAt(index * blockBytes_, bytes);
	if (!read.hasValue()) {
		return read.error();
	}
	bytes.resize(read.value());
	evictIfFull();
	blocks_.push_front(CachedBlock{.index = index, .bytes = std::move(bytes)});
	held_.insert_or_assign(index, blocks_.begin());
	return &blocks_.front();
}

void CachingDevice::evictIfFull() {
	if (blocks_.size() < blockCount_) {
		return;
	}
	held_.erase(blocks_.back().index);
	blocks_.pop_back();
}

} // namespace revenant
