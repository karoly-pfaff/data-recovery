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

[[nodiscard]] std::vector<std::byte> filledImage() {
	return std::vector<std::byte>(kDeviceBytes, kFill);
}

// A stack over a device the test still wants to talk about, which is why the
// device is built here and handed in rather than opened from a path.
[[nodiscard]] SourceStack stackOver(std::vector<Fault> faults) {
	return SourceStack::over(
		std::make_unique<FaultyDevice>(filledImage(), kSector, std::move(faults)));
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

TEST(SourceStack, AnUndamagedSourceReadsItsOwnBytesAndReportsNoDamage) {
	auto stack = stackOver({});
	EXPECT_EQ(readAt(stack, Window{.offset = 0, .length = kSector}), std::vector<std::byte>(kSector, kFill));
	EXPECT_TRUE(stack.badRanges().empty());
}

// The whole point of the composition: the run does not stop, and it does not
// pretend. The refused sector comes back as zeros and is named.
TEST(SourceStack, ARefusedSectorComesBackAsZerosAndIsRecorded) {
	auto stack = stackOver({Fault{.offsetBytes = kSector, .lengthBytes = kSector}});
	const auto got = readAt(stack, Window{.offset = 0, .length = 2 * kSector});
	EXPECT_EQ(
		std::vector<std::byte>(got.begin(), got.begin() + kSector),
		std::vector<std::byte>(kSector, kFill));
	EXPECT_EQ(
		std::vector<std::byte>(got.begin() + kSector, got.end()),
		std::vector<std::byte>(kSector, std::byte{0}));
	ASSERT_EQ(stack.badRanges().size(), 1U);
	EXPECT_EQ(
		stack.badRanges().front(),
		(BadRange{.offsetBytes = kSector, .lengthBytes = kSector}));
}

// The decorators hold their source by reference, so a stack that was moved
// after being built would leave them pointing at a destroyed device. `openSource`
// returns one by value, so this is not a hypothetical.
TEST(SourceStack, SurvivesBeingMoved) {
	auto built = stackOver({Fault{.offsetBytes = kSector, .lengthBytes = kSector}});
	SourceStack moved = std::move(built);
	EXPECT_EQ(readAt(moved, Window{.offset = 0, .length = kSector}), std::vector<std::byte>(kSector, kFill));
	EXPECT_EQ(readAt(moved, Window{.offset = kSector, .length = kSector}), std::vector<std::byte>(kSector, std::byte{0}));
	EXPECT_EQ(moved.badRanges().size(), 1U);
}

} // namespace
