// SPDX-License-Identifier: GPL-3.0-or-later
// A region says where a scan *looks*, not what a file may be. Everything here
// pins one of those two halves.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/ScanCandidate.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Confidence.hpp"
#include "support/CollectingVisitor.hpp"
#include "support/FakeCarver.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

using revenant::Confidence;
using revenant::carve::CarverRegistry;
using revenant::carve::ScanConfig;
using revenant::carve::ScanRegion;
using revenant::carve::SignatureScanner;
using revenant::testing::CollectingVisitor;
using revenant::testing::FakeCarver;
using revenant::testing::InMemoryDevice;

constexpr std::uint32_t kSector = 512;
constexpr std::size_t kDeviceBytes = 4096;

[[nodiscard]] std::vector<std::byte> withMagicAt(std::initializer_list<std::size_t> positions) {
	std::vector<std::byte> bytes(kDeviceBytes, std::byte{0});
	for (const auto at : positions) {
		bytes.at(at) = std::byte{0xAB};
		bytes.at(at + 1) = std::byte{0xCD};
	}
	return bytes;
}

// A magic every `stride` bytes, so a multi-window region has something to find
// in more than one of its windows.
[[nodiscard]] std::vector<std::byte> withMagicEvery(std::size_t stride) {
	std::vector<std::byte> bytes(kDeviceBytes, std::byte{0});
	for (std::size_t at = stride; at + 1 < kDeviceBytes; at += stride) {
		bytes.at(at) = std::byte{0xAB};
		bytes.at(at + 1) = std::byte{0xCD};
	}
	return bytes;
}

[[nodiscard]] CarverRegistry validRegistry(std::uint64_t carveLength) {
	CarverRegistry registry;
	registry.registerCarver(std::make_unique<FakeCarver>(Confidence::kValid, carveLength));
	return registry;
}

// One scan of `bytes` restricted to `region`, reported into the visitor.
class RegionScan {
public:
	RegionScan(std::vector<std::byte> bytes, ScanRegion region, std::uint64_t carveLength)
		: device_(std::move(bytes), kSector), registry_(validRegistry(carveLength)) {
		const auto stats =
			SignatureScanner{registry_, ScanConfig{}}.scanRegion(device_, region, visitor_);
		EXPECT_TRUE(stats.hasValue());
	}

	[[nodiscard]] const std::vector<revenant::carve::ScanCandidate>& candidates() const {
		return visitor_.candidates();
	}

private:
	InMemoryDevice device_;
	CarverRegistry registry_;
	CollectingVisitor visitor_;
};

TEST(ScanRegion, ReportsOnlyTheMatchInsideTheRegion) {
	const RegionScan scan{
		withMagicAt({100, 1500, 3000}),
		ScanRegion{.offset = 1000, .lengthBytes = 1000},
		16};
	ASSERT_EQ(scan.candidates().size(), 1U);
	EXPECT_EQ(scan.candidates().front().offset, 1500U);
}

TEST(ScanRegion, IncludesAMatchStartingOnTheRegionsFirstByte) {
	const RegionScan scan{withMagicAt({1000}), ScanRegion{.offset = 1000, .lengthBytes = 100}, 16};
	ASSERT_EQ(scan.candidates().size(), 1U);
	EXPECT_EQ(scan.candidates().front().offset, 1000U);
}

// The magic's second byte lies past the region, so the read has to reach a
// little further than the region itself to see the match whole.
TEST(ScanRegion, IncludesAMatchStartingOnTheRegionsLastByte) {
	const RegionScan scan{withMagicAt({1099}), ScanRegion{.offset = 1000, .lengthBytes = 100}, 16};
	ASSERT_EQ(scan.candidates().size(), 1U);
	EXPECT_EQ(scan.candidates().front().offset, 1099U);
}

TEST(ScanRegion, ExcludesAMatchStartingOneByteAfterTheRegion) {
	const RegionScan scan{withMagicAt({1100}), ScanRegion{.offset = 1000, .lengthBytes = 100}, 16};
	EXPECT_TRUE(scan.candidates().empty());
}

// The boundary is an artifact of what some other file claimed, not of this
// one: truncating here would turn a whole recovery into a fragment.
TEST(ScanRegion, KeepsTheFullLengthOfAFileRunningPastTheRegion) {
	const RegionScan scan{withMagicAt({1090}), ScanRegion{.offset = 1000, .lengthBytes = 100}, 512};
	ASSERT_EQ(scan.candidates().size(), 1U);
	EXPECT_EQ(scan.candidates().front().result.length, 512U);
}

TEST(ScanRegion, AnEmptyRegionReportsNothing) {
	const RegionScan scan{withMagicAt({1000}), ScanRegion{.offset = 1000, .lengthBytes = 0}, 16};
	EXPECT_TRUE(scan.candidates().empty());
}

TEST(ScanRegion, ARegionPastTheDeviceEndReportsNothing) {
	const RegionScan scan{
		withMagicAt({1000}),
		ScanRegion{.offset = kDeviceBytes * 2, .lengthBytes = 1000},
		16};
	EXPECT_TRUE(scan.candidates().empty());
}

// Several windows' worth of region, so the loop's own bookkeeping is exercised
// rather than a single read that happens to cover everything.
TEST(ScanRegion, FindsEveryMatchAcrossAMultiWindowRegion) {
	InMemoryDevice device{withMagicEvery(300), kSector};
	const auto registry = validRegistry(16);
	CollectingVisitor visitor;
	ScanConfig config;
	config.windowBytes = 256;
	const auto stats = SignatureScanner{registry, config}.scanRegion(
		device,
		ScanRegion{.offset = 500, .lengthBytes = 1000},
		visitor);
	ASSERT_TRUE(stats.hasValue());
	ASSERT_EQ(visitor.candidates().size(), 3U);
	EXPECT_EQ(visitor.candidates().front().offset, 600U);
	EXPECT_EQ(visitor.candidates().back().offset, 1200U);
}

TEST(ScanRegion, ReportsTheRegionsLengthAsBytesScanned) {
	InMemoryDevice device{std::vector<std::byte>(kDeviceBytes, std::byte{0}), kSector};
	const auto registry = validRegistry(16);
	CollectingVisitor visitor;
	const auto stats = SignatureScanner{
		registry,
		ScanConfig{}}.scanRegion(device, ScanRegion{.offset = 1000, .lengthBytes = 100}, visitor);
	ASSERT_TRUE(stats.hasValue());
	EXPECT_EQ(stats.value().bytesScanned, 100U);
}

} // namespace
