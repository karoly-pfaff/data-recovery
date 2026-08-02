// SPDX-License-Identifier: GPL-3.0-or-later
// story-0604: the device stack a real run reads through, composed for the first
// time. What is under test is that composing it changes what a caller *learns*
// and not what it reads: the same bytes come back, a refused sector comes back
// as zeros rather than as a failure, and the fact that those zeros were invented
// is on the stack for the manifest to state.
#include "revenant/core/io/SourceStack.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "revenant/core/io/BadRange.hpp"
#include "support/FaultyDevice.hpp"

namespace {

using revenant::BadRange;
using revenant::SourceStack;
using revenant::testing::Fault;
using revenant::testing::FaultyDevice;

constexpr std::uint32_t kSector = 512;
constexpr std::size_t kDeviceBytes = std::size_t{64} * 1024;

// A byte that is not zero, so a zero-filled hole is distinguishable from the
// device's own content.
constexpr std::byte kFill{0xA5};

// A stack over a device the test still wants to talk about, which is why the
// device is built here and handed in rather than opened from a path.
[[nodiscard]] SourceStack stackOver(std::vector<Fault> faults) {
	return SourceStack::over(
		std::make_unique<FaultyDevice>(
			std::vector<std::byte>(kDeviceBytes, kFill),
			kSector,
			std::move(faults)));
}

// What to read, as one thing: two bare integers in a row are two chances to
// pass them the wrong way round.
struct Window {
	std::uint64_t offset;
	std::size_t length;
};

[[nodiscard]] std::vector<std::byte> readAt(SourceStack& stack, Window window) {
	std::vector<std::byte> got(window.length, std::byte{0});
	EXPECT_TRUE(stack.top().readAt(window.offset, got).hasValue());
	return got;
}

// One sector of the device's own content, and two, as the tests below ask for
// them — named here so no case spells out an arithmetic expression.
constexpr Window kFirstSector{.offset = 0, .length = kSector};
constexpr Window kFirstTwoSectors{.offset = 0, .length = std::size_t{2} * kSector};
constexpr Window kSecondSector{.offset = kSector, .length = kSector};

[[nodiscard]] std::vector<std::byte> filledSector() {
	return std::vector<std::byte>(kSector, kFill);
}

[[nodiscard]] std::vector<std::byte> zeroedSector() {
	return std::vector<std::byte>(kSector, std::byte{0});
}

TEST(SourceStack, AnUndamagedSourceReadsItsOwnBytesAndReportsNoDamage) {
	auto stack = stackOver({});
	EXPECT_EQ(readAt(stack, kFirstSector), filledSector());
	EXPECT_TRUE(stack.badRanges().empty());
}

// The whole point of the composition: the run does not stop, and it does not
// pretend. The refused sector comes back as zeros and is named.
TEST(SourceStack, ARefusedSectorComesBackAsZerosAndIsRecorded) {
	auto stack = stackOver({Fault{.offsetBytes = kSector, .lengthBytes = kSector}});
	const auto got = readAt(stack, kFirstTwoSectors);
	EXPECT_EQ(std::vector<std::byte>(got.begin(), got.begin() + kSector), filledSector());
	EXPECT_EQ(std::vector<std::byte>(got.begin() + kSector, got.end()), zeroedSector());
	ASSERT_EQ(stack.badRanges().size(), 1U);
	EXPECT_EQ(
		stack.badRanges().front(),
		(BadRange{.offsetBytes = kSector, .lengthBytes = kSector}));
}

// A real run reads a bad sector twice — once scanning, once extracting — and
// nothing between the run and the device remembers the first read. The map has
// to be a set, or the whole run reports twice the damage there is.
TEST(SourceStack, ABadSectorMetTwiceIsStillOneRange) {
	auto stack = stackOver({Fault{.offsetBytes = 0, .lengthBytes = kSector}});
	EXPECT_EQ(readAt(stack, kFirstSector), zeroedSector());
	EXPECT_EQ(readAt(stack, kFirstSector), zeroedSector());
	ASSERT_EQ(stack.badRanges().size(), 1U);
	EXPECT_EQ(stack.badRanges().front(), (BadRange{.offsetBytes = 0, .lengthBytes = kSector}));
}

// The decorators hold their source by reference, so a stack that was moved
// after being built would leave them pointing at a destroyed device. `openSource`
// returns one by value, so this is not a hypothetical.
TEST(SourceStack, SurvivesBeingMoved) {
	auto built = stackOver({Fault{.offsetBytes = kSector, .lengthBytes = kSector}});
	SourceStack moved = std::move(built);
	EXPECT_EQ(readAt(moved, kFirstSector), filledSector());
	EXPECT_EQ(readAt(moved, kSecondSector), zeroedSector());
	EXPECT_EQ(moved.badRanges().size(), 1U);
}

} // namespace
