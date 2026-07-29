// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/carve/SignatureTable.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"

namespace {

using revenant::carve::CarverRegistry;
using revenant::carve::registerBuiltinCarvers;

// Two signatures, both starting with the same byte, so a group holds more than
// one entry and their order inside it can be asserted.
class TwoMagicCarver final : public revenant::carve::FormatCarver {
public:
	[[nodiscard]] std::span<const revenant::carve::Signature> signatures() const override {
		return signatures_;
	}

	[[nodiscard]] revenant::Result<revenant::carve::CarveResult>
	carve(revenant::ByteReader& reader) const override {
		static_cast<void>(reader);
		return revenant::carve::CarveResult{
			.length = 1,
			.confidence = revenant::Confidence::kRejected,
			.extension = "fake"};
	}

private:
	static constexpr std::array<std::byte, 2> kFirst{std::byte{0x7A}, std::byte{0x01}};
	static constexpr std::array<std::byte, 3> kSecond{
		std::byte{0x7A},
		std::byte{0x02},
		std::byte{0x03}};
	std::array<revenant::carve::Signature, 2> signatures_{
		revenant::carve::Signature{.magic = kFirst, .offset = 0},
		revenant::carve::Signature{.magic = kSecond, .offset = 9}};
};

[[nodiscard]] CarverRegistry withBuiltins() {
	CarverRegistry registry;
	registerBuiltinCarvers(registry);
	return registry;
}

TEST(SignatureTable, HoldsEveryRegisteredSignature) {
	const auto registry = withBuiltins();
	std::size_t declared = 0;
	for (const auto& carver : registry.carvers()) {
		declared += carver->signatures().size();
	}
	EXPECT_EQ(registry.signatureTable().size(), declared);
}

TEST(SignatureTable, AnEmptyRegistryMatchesNoByte) {
	const CarverRegistry registry;
	EXPECT_TRUE(registry.signatureTable().startingWith(std::byte{0xFF}).empty());
}

// The reject that runs once per byte of a device: almost every byte answers
// nothing, and that answer must be an empty group rather than a search.
TEST(SignatureTable, ByteNoSignatureStartsWithHasAnEmptyGroup) {
	const auto registry = withBuiltins();
	EXPECT_TRUE(registry.signatureTable().startingWith(std::byte{0x7F}).empty());
}

// Every registered magic's first byte, so the assertion below is one loop
// rather than two and reads as the question it is asking.
[[nodiscard]] std::vector<std::byte> firstBytesIn(const CarverRegistry& registry) {
	std::vector<std::byte> first;
	for (const auto& carver : registry.carvers()) {
		for (const auto& signature : carver->signatures()) {
			first.push_back(signature.magic.front());
		}
	}
	return first;
}

TEST(SignatureTable, FindsEachRegisteredMagicUnderItsOwnFirstByte) {
	const auto registry = withBuiltins();
	for (const std::byte first : firstBytesIn(registry)) {
		EXPECT_FALSE(registry.signatureTable().startingWith(first).empty())
			<< "no group for a registered magic's first byte";
	}
}

TEST(SignatureTable, CarriesTheInFileOffsetTheMagicWasDeclaredWith) {
	CarverRegistry registry;
	registry.registerCarver(std::make_unique<TwoMagicCarver>());
	const auto group = registry.signatureTable().startingWith(std::byte{0x7A});
	ASSERT_EQ(group.size(), 2U);
	EXPECT_EQ(group.front().inFileOffset, 0U);
	EXPECT_EQ(group.back().inFileOffset, 9U);
}

// Two carvers sharing a first byte both land in that byte's group, in the order
// they were registered — which is the order the match sequence is tied to.
TEST(SignatureTable, GroupsShareABucketInRegistrationOrder) {
	CarverRegistry registry;
	registry.registerCarver(std::make_unique<TwoMagicCarver>());
	registry.registerCarver(std::make_unique<TwoMagicCarver>());
	const auto group = registry.signatureTable().startingWith(std::byte{0x7A});
	ASSERT_EQ(group.size(), 4U);
	EXPECT_EQ(group.front().carverIndex, 0U);
	EXPECT_EQ(group.back().carverIndex, 1U);
}

// Registering another carver rebuilds the table rather than leaving it behind:
// the scanner asks the registry, and a stale table would search for less.
TEST(SignatureTable, GrowsWhenAnotherCarverIsRegistered) {
	CarverRegistry registry;
	registry.registerCarver(std::make_unique<TwoMagicCarver>());
	const auto before = registry.signatureTable().size();
	registry.registerCarver(std::make_unique<TwoMagicCarver>());
	EXPECT_EQ(registry.signatureTable().size(), before * 2);
}

} // namespace
