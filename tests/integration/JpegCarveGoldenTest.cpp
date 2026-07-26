// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <vector>

#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Confidence.hpp"
#include "support/CollectingVisitor.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

using revenant::Confidence;

// Same 22-byte structural JPEG as the unit fixture (duplicated by design:
// the unit test must not share fixtures with integration — independent
// failure domains; jscpd min-lines 8 is not reached by data tables).
std::vector<std::byte> goldenJpeg() {
	auto b = [](int v) { return static_cast<std::byte>(v); };
	return {b(0xFF), b(0xD8), b(0xFF), b(0xE0), b(0x00), b(0x04), b(0x4A), b(0x46),
			b(0xFF), b(0xDA), b(0x00), b(0x02), b(0x01), b(0xFF), b(0x00), b(0x02),
			b(0xFF), b(0xD3), b(0x03), b(0x04), b(0xFF), b(0xD9)};
}

TEST(JpegCarveGolden, EmbeddedJpegIsRecoveredByteIdentical) {
	const auto jpeg = goldenJpeg();
	std::vector<std::byte> device(8192, std::byte{0x5A});
	constexpr std::size_t kPlantOffset = 1000;
	std::ranges::copy(jpeg, device.begin() + kPlantOffset);
	revenant::testing::InMemoryDevice source{device, 512};
	revenant::carve::CarverRegistry registry;
	revenant::carve::registerBuiltinCarvers(registry);
	revenant::testing::CollectingVisitor visitor;
	const auto stats =
		revenant::carve::SignatureScanner{registry, revenant::carve::ScanConfig{}}.scan(
			source,
			visitor);
	ASSERT_TRUE(stats.hasValue());
	ASSERT_EQ(visitor.candidates().size(), 1U);
	const auto& candidate = visitor.candidates().front();
	EXPECT_EQ(candidate.offset, kPlantOffset);
	EXPECT_EQ(candidate.result.length, jpeg.size());
	EXPECT_EQ(candidate.result.confidence, Confidence::kValid);
	const std::vector<std::byte> carved(
		device.begin() + kPlantOffset,
		device.begin() + kPlantOffset + static_cast<std::ptrdiff_t>(jpeg.size()));
	EXPECT_EQ(carved, jpeg); // byte-identical to the original
}

TEST(JpegCarveGolden, TwoJpegsBackToBackAreBothFound) {
	const auto jpeg = goldenJpeg();
	std::vector<std::byte> device(4096, std::byte{0});
	std::ranges::copy(jpeg, device.begin() + 100);
	std::ranges::copy(jpeg, device.begin() + 100 + static_cast<std::ptrdiff_t>(jpeg.size()));
	revenant::testing::InMemoryDevice source{device, 512};
	revenant::carve::CarverRegistry registry;
	revenant::carve::registerBuiltinCarvers(registry);
	revenant::testing::CollectingVisitor visitor;
	const revenant::carve::SignatureScanner scanner{registry, revenant::carve::ScanConfig{}};
	ASSERT_TRUE(scanner.scan(source, visitor).hasValue());
	ASSERT_EQ(visitor.candidates().size(), 2U);
	EXPECT_EQ(visitor.candidates().at(1).offset, 100 + jpeg.size());
}

} // namespace
