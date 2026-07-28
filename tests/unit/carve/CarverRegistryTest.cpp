// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/carve/CarverRegistry.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string_view>

#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/core/Confidence.hpp"
#include "support/FakeCarver.hpp"

namespace {

using revenant::Confidence;
using revenant::carve::builtinFormatNames;
using revenant::carve::CarverRegistry;
using revenant::carve::isBuiltinFormat;
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

// What makes an allowlist entry meaningful, and what a frontend may offer.
TEST(CarverRegistry, KnowsEveryExtensionItsCarversReport) {
	for (const std::string_view name :
		 {"jpg",
		  "png",
		  "mp4",
		  "mov",
		  "cr2",
		  "nef",
		  "arw",
		  "tif",
		  "zip",
		  "docx",
		  "xlsx",
		  "pptx",
		  "pdf"}) {
		EXPECT_TRUE(isBuiltinFormat(name)) << name;
	}
}

// The offered names and the registering ones are the same lists flattened, and
// this is what says so: allowing all of them keeps every carver, so a format
// added to one carver's list cannot go missing from what a frontend offers.
TEST(CarverRegistry, TheNamesItOffersKeepEveryCarverItShips) {
	CarverRegistry named;
	revenant::carve::registerBuiltinCarvers(named, builtinFormatNames());
	CarverRegistry all;
	revenant::carve::registerBuiltinCarvers(all);
	EXPECT_EQ(named.carvers().size(), all.carvers().size());
	EXPECT_TRUE(std::ranges::all_of(builtinFormatNames(), isBuiltinFormat));
}

// Both look right and are wrong: the RAW carver reports `tif`, the JPEG carver
// `jpg`. Left unchecked, either would silently narrow a scan to nothing.
TEST(CarverRegistry, DoesNotKnowANameNoCarverAnswersTo) {
	EXPECT_FALSE(isBuiltinFormat("tiff"));
	EXPECT_FALSE(isBuiltinFormat("jpeg"));
	EXPECT_FALSE(isBuiltinFormat(""));
}

} // namespace
