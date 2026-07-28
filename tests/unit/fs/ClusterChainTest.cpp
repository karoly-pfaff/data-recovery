// SPDX-License-Identifier: GPL-3.0-or-later
// story-0031: following a chain, and refusing to follow one that no longer
// describes a file. The rejections matter more than the happy path here — a
// freed chain looks exactly like a short one, and reporting it as a short file
// would hand back the wrong bytes.
#include "fs/ClusterChain.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "revenant/core/Error.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

using revenant::ErrorCode;
using revenant::toLittleEndian;
using revenant::fs::chainExtents;
using revenant::fs::ClusterChain;
using revenant::fs::ClusterGeometry;
using revenant::testing::InMemoryDevice;

constexpr std::uint32_t kSectorSize = 512;
constexpr std::uint32_t kClusterBytes = 2048;
constexpr std::uint64_t kFatOffset = 16384;
constexpr std::uint64_t kFatSize = 32768;
constexpr std::uint64_t kDataOffset = 81920;
constexpr std::uint64_t kClusters = 100;

constexpr std::uint32_t kEndOfChain = 0x0FFF'FFFF;
constexpr std::uint32_t kBadCluster = 0x0FFF'FFF7;
constexpr std::uint32_t kFreeEntry = 0;

[[nodiscard]] ClusterGeometry geometry() {
	return ClusterGeometry{
		.bytesPerCluster = kClusterBytes,
		.tableOffsetBytes = kFatOffset,
		.tableSizeBytes = kFatSize,
		.dataOffsetBytes = kDataOffset,
		.totalClusters = kClusters};
}

// One FAT entry as the fixture states it: which cluster, and where it points.
struct Entry {
	std::uint32_t cluster;
	std::uint32_t next;
};

// A volume that is nothing but a FAT: the chain follower never reads anything
// else, so nothing else needs to be there.
class Volume {
public:
	Volume() : image_(static_cast<std::size_t>(kDataOffset), std::byte{0}) {}

	void put(Entry entry) {
		const auto raw = toLittleEndian<std::uint32_t>(entry.next);
		const auto at = static_cast<std::size_t>(kFatOffset + (std::uint64_t{entry.cluster} * 4));
		for (std::size_t byte = 0; byte < raw.size(); ++byte) {
			image_.at(at + byte) = raw.at(byte);
		}
	}

	[[nodiscard]] ClusterChain mount() {
		device_ = std::make_unique<InMemoryDevice>(image_, kSectorSize);
		return ClusterChain{*device_, geometry()};
	}

private:
	std::vector<std::byte> image_;
	std::unique_ptr<InMemoryDevice> device_;
};

[[nodiscard]] std::vector<std::uint32_t> chainOf(Volume& volume, std::uint32_t first) {
	const auto table = volume.mount();
	const auto chain = table.chainFrom(first);
	EXPECT_TRUE(chain.hasValue());
	return chain.hasValue() ? chain.value() : std::vector<std::uint32_t>{};
}

[[nodiscard]] ErrorCode refusalOf(Volume& volume, std::uint32_t first) {
	const auto table = volume.mount();
	const auto chain = table.chainFrom(first);
	EXPECT_FALSE(chain.hasValue());
	return chain.hasValue() ? ErrorCode::kNotFound : chain.error().code;
}

TEST(ClusterChain, AOneClusterFileEndsAtItsFirstCluster) {
	Volume volume;
	volume.put({.cluster = 5, .next = kEndOfChain});
	EXPECT_EQ(chainOf(volume, 5), (std::vector<std::uint32_t>{5}));
}

TEST(ClusterChain, FollowsAChainToItsEnd) {
	Volume volume;
	volume.put({.cluster = 5, .next = 6});
	volume.put({.cluster = 6, .next = 7});
	volume.put({.cluster = 7, .next = kEndOfChain});
	EXPECT_EQ(chainOf(volume, 5), (std::vector<std::uint32_t>{5, 6, 7}));
}

// A chain into a free entry is what deletion leaves behind. Reporting it as a
// shorter file would hand back bytes the file never owned.
TEST(ClusterChain, AChainIntoAFreedEntryIsRefused) {
	Volume volume;
	volume.put({.cluster = 5, .next = 6});
	volume.put({.cluster = 6, .next = kFreeEntry});
	EXPECT_EQ(refusalOf(volume, 5), ErrorCode::kInvalidArgument);
}

TEST(ClusterChain, AChainIntoABadClusterIsRefused) {
	Volume volume;
	volume.put({.cluster = 5, .next = kBadCluster});
	EXPECT_EQ(refusalOf(volume, 5), ErrorCode::kInvalidArgument);
}

TEST(ClusterChain, AChainLeavingTheDataRegionIsRefused) {
	Volume volume;
	volume.put({.cluster = 5, .next = static_cast<std::uint32_t>(kClusters + 10)});
	EXPECT_EQ(refusalOf(volume, 5), ErrorCode::kInvalidArgument);
}

// A crafted cycle must cost a bounded walk, not an unbounded one (ADR-0009).
TEST(ClusterChain, ACycleIsBoundedByTheVolumesOwnClusterCount) {
	Volume volume;
	volume.put({.cluster = 5, .next = 6});
	volume.put({.cluster = 6, .next = 5});
	EXPECT_EQ(refusalOf(volume, 5), ErrorCode::kOutOfRange);
}

TEST(ClusterChain, AFirstClusterOutsideTheDataRegionIsRefused) {
	Volume volume;
	EXPECT_EQ(refusalOf(volume, 0), ErrorCode::kInvalidArgument);
}

TEST(ChainExtents, ConsecutiveClustersCoalesceIntoOneExtent) {
	const std::vector<std::uint32_t> clusters{5, 6, 7};
	const auto extents = chainExtents(clusters, geometry(), std::uint64_t{3} * kClusterBytes);
	ASSERT_TRUE(extents.hasValue());
	EXPECT_EQ(extents.value().size(), 1U);
	EXPECT_EQ(extents.value().front().lengthBytes, std::uint64_t{3} * kClusterBytes);
}

TEST(ChainExtents, AGapStartsANewExtent) {
	const std::vector<std::uint32_t> clusters{5, 6, 20};
	const auto extents = chainExtents(clusters, geometry(), std::uint64_t{3} * kClusterBytes);
	ASSERT_TRUE(extents.hasValue());
	EXPECT_EQ(extents.value().size(), 2U);
}

// A file never fills its last cluster exactly, and handing back the slack would
// append whatever the volume happened to leave there.
TEST(ChainExtents, TheTailIsTrimmedToTheDeclaredSize) {
	const std::vector<std::uint32_t> clusters{5, 6};
	const auto extents = chainExtents(clusters, geometry(), 3000);
	ASSERT_TRUE(extents.hasValue());
	EXPECT_EQ(extents.value().front().lengthBytes, 3000U);
}

TEST(ChainExtents, ASizeLargerThanTheChainAllocatesIsRefused) {
	const std::vector<std::uint32_t> clusters{5};
	const auto extents = chainExtents(clusters, geometry(), std::uint64_t{10} * kClusterBytes);
	ASSERT_FALSE(extents.hasValue());
	EXPECT_EQ(extents.error().code, ErrorCode::kInvalidArgument);
}

} // namespace
