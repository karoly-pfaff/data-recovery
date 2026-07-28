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

// A structurally valid JPEG large enough to be a plausible file: the
// plausibility floor (story-0025) exists precisely to reject the 22-byte
// version this fixture used to be, so a golden test built on one would have
// been asserting behaviour no real scan produces.
// (Duplicated from the unit fixture by design: the unit test must not share
// fixtures with integration — independent failure domains.)
std::byte b(int value) {
	return static_cast<std::byte>(value);
}

// Entropy-coded payload: deterministic, and never a raw 0xFF, so the marker
// walk runs to the EOI instead of stopping on a stray marker.
void appendEntropy(std::vector<std::byte>& jpeg, std::size_t count) {
	for (std::size_t i = 0; i < count; ++i) {
		jpeg.push_back(b(static_cast<int>(i % 0xFE)));
	}
}

std::vector<std::byte> goldenJpeg() {
	std::vector<std::byte> jpeg{
		b(0xFF),
		b(0xD8),
		b(0xFF),
		b(0xE0),
		b(0x00),
		b(0x04),
		b(0x4A),
		b(0x46),
		b(0xFF),
		b(0xDA),
		b(0x00),
		b(0x02)};
	appendEntropy(jpeg, 600);
	jpeg.push_back(b(0xFF));
	jpeg.push_back(b(0xD9));
	return jpeg;
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
