// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <span>
#include <unordered_map>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant {

// Big enough that one block covers a boot sector, an MFT record and the
// directory blocks around it; small enough that a modest cache of them still
// fits in a few megabytes.
inline constexpr std::size_t kDefaultCacheBlockBytes = std::size_t{64} * 1024;
inline constexpr std::size_t kDefaultCacheBlocks = 64;

// How much the cache holds, and in what units. Policy chosen by whoever composes
// the device stack — never a number read off a disk — so an unusable value is
// clamped rather than refused (`PartitionView`'s rule).
struct CacheShape {
	std::size_t blockBytes = kDefaultCacheBlockBytes;
	std::size_t blockCount = kDefaultCacheBlocks;
};

// A least-recently-used cache of fixed-size, aligned blocks over another device.
// Parsing reads the same few hundred bytes many times and in overlapping pieces;
// this turns that into one read per block.
//
// It also makes every read it issues *aligned* — offset and length are always a
// whole block — which is what a raw physical device requires on Windows. A
// caching layer over such a device therefore satisfies the alignment rule by
// construction, and nothing above it has to know what a sector is.
class CachingDevice final : public BlockDevice {
public:
	// `shape` is clamped: a block size below the source's sector size or not a
	// power of two is rounded up, and a cache of no blocks becomes a cache of one.
	CachingDevice(BlockDevice& source, const CacheShape& shape);

	[[nodiscard]] std::uint64_t sizeInBytes() const override;
	[[nodiscard]] std::uint32_t sectorSize() const override;
	[[nodiscard]] Result<std::size_t>
	readAt(std::uint64_t offset, std::span<std::byte> buffer) override;

	// How many blocks are held right now — the cache's own bookkeeping, exposed
	// so a test can assert that eviction happened rather than infer it.
	[[nodiscard]] std::size_t heldBlocks() const noexcept;

private:
	// One cached block: which block of the device it is, and the bytes that were
	// actually there. The last block of a device is short, and remembering the
	// requested length instead of the read one would hand back trailing zeros as
	// if they were on the disk.
	struct CachedBlock {
		std::uint64_t index = 0;
		std::vector<std::byte> bytes;
	};

	// How far a copy has got, and whether the device had more to give. A block
	// that supplies nothing is the end of the device rather than a short step to
	// be retried.
	struct CopyStep {
		std::size_t done = 0;
		bool more = false;
	};

	using Blocks = std::list<CachedBlock>;

	[[nodiscard]] Result<std::size_t>
	copyThroughCache(std::uint64_t offset, std::span<std::byte> buffer);
	[[nodiscard]] Result<CopyStep>
	advancedBy(std::uint64_t offset, std::span<std::byte> buffer, std::size_t done);
	[[nodiscard]] Result<std::size_t>
	copyOneBlock(std::uint64_t offset, std::span<std::byte> buffer);
	// Non-owning; valid until the next load evicts something.
	[[nodiscard]] Result<const CachedBlock*> blockAt(std::uint64_t index);
	[[nodiscard]] Result<const CachedBlock*> loadBlock(std::uint64_t index);
	void evictIfFull();

	BlockDevice& source_;
	std::uint64_t blockBytes_;
	std::size_t blockCount_;
	Blocks blocks_;
	std::unordered_map<std::uint64_t, Blocks::iterator> held_;
};

} // namespace revenant
