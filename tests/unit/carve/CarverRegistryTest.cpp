// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/carve/CarverRegistry.hpp"

#include <gtest/gtest.h>

#include <memory>

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

} // namespace
