// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/carve/CarverRegistry.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string_view>

#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/core/Confidence.hpp"
#include "support/FakeCarver.hpp"

namespace {

using revenant::Confidence;
using revenant::carve::CarverRegistry;
using revenant::testing::FakeCarver;

TEST(CarverRegistry, StartsEmpty) {
	const CarverRegistry registry;
	EXPECT_TRUE(registry.carvers().empty());
	EXPECT_EQ(registry.maxSignatureBytes(), 0U);
}

TEST(CarverRegistry, OwnsRegisteredCarvers) {
	CarverRegistry registry;
	registry.registerCarver(std::make_unique<FakeCarver>(Confidence::kValid, 8));
	ASSERT_EQ(registry.carvers().size(), 1U);
	EXPECT_FALSE(registry.carvers().front()->signatures().empty());
}

TEST(CarverRegistry, ReportsWidestSignatureSpan) {
	CarverRegistry registry;
	registry.registerCarver(std::make_unique<FakeCarver>(Confidence::kValid, 8));
	EXPECT_EQ(registry.maxSignatureBytes(), 2U); // FakeCarver magic is 2 bytes at offset 0
}

TEST(CarverRegistry, AnAllowlistRegistersOnlyTheNamedFormats) {
	constexpr std::array<std::string_view, 1> kOnlyJpeg{"jpg"};
	CarverRegistry registry;
	revenant::carve::registerBuiltinCarvers(registry, kOnlyJpeg);
	EXPECT_EQ(registry.carvers().size(), 1U);
}

TEST(CarverRegistry, AnAllowlistNamingOneOfACarversFormatsKeepsThatCarver) {
	constexpr std::array<std::string_view, 1> kOnlyNef{"nef"};
	CarverRegistry registry;
	revenant::carve::registerBuiltinCarvers(registry, kOnlyNef);
	EXPECT_EQ(registry.carvers().size(), 1U);
}

TEST(CarverRegistry, AnEmptyAllowlistRegistersEverything) {
	CarverRegistry all;
	revenant::carve::registerBuiltinCarvers(all);
	CarverRegistry empty;
	revenant::carve::registerBuiltinCarvers(empty, {});
	EXPECT_EQ(empty.carvers().size(), all.carvers().size());
	EXPECT_GT(all.carvers().size(), 1U);
}

} // namespace
