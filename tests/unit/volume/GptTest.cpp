// SPDX-License-Identifier: GPL-3.0-or-later
// story-0044: the GPT header and one of its entries. Every field a rejection
// names here is a number the reader would otherwise act on, which is why the
// checksum is asserted to come first — and why each field case re-signs the
// header, so that what is rejected is the field and not the checksum the change
// invalidated.
#include "revenant/volume/Gpt.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "support/GptFixture.hpp"
#include "support/Rejection.hpp"

namespace {

using revenant::toLittleEndian;
using revenant::testing::GptEntrySpec;
using revenant::testing::GptHeaderSpec;
using revenant::testing::gptTypeGuid;
using revenant::testing::invalidAt;
using revenant::testing::makeGptEntry;
using revenant::testing::makeGptHeader;
using revenant::testing::outOfRangeAt;
using revenant::testing::Rejection;
using revenant::testing::signGptHeader;
using revenant::volume::GptEntry;
using revenant::volume::GptHeader;
using revenant::volume::isUnusedEntry;
using revenant::volume::kGptEntryBytes;
using revenant::volume::parseGptEntry;
using revenant::volume::parseGptHeader;

constexpr std::uint64_t kHeaderLba = 1;
constexpr std::uint64_t kCrcOffset = 0x10;
constexpr std::uint32_t kArrayCrc = 0x12345678;
constexpr std::uint8_t kTypeSeed = 0xAB;

constexpr GptHeaderSpec kValidSpec{.entryArrayCrc = kArrayCrc};

void writeLe(std::vector<std::byte>& bytes, std::size_t offset, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(raw, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

// One field rewritten and the header signed again, so a rejection names that
// field rather than the checksum the rewrite invalidated.
[[nodiscard]] std::vector<std::byte> headerWith(std::size_t offset, auto value) {
	auto header = makeGptHeader(kValidSpec);
	writeLe(header, offset, value);
	signGptHeader(header);
	return header;
}

[[nodiscard]] Rejection rejectionOf(std::span<const std::byte> header) {
	return revenant::testing::rejectionOf(parseGptHeader(header, kHeaderLba));
}

[[nodiscard]] std::vector<std::byte> makeUsedEntry(std::string_view name) {
	return makeGptEntry(
		GptEntrySpec{.typeSeed = kTypeSeed, .firstLba = 100, .lastLba = 199, .name = name});
}

TEST(Gpt, ReadsEveryFieldOfAValidHeader) {
	const auto parsed = parseGptHeader(makeGptHeader(kValidSpec), kHeaderLba);
	ASSERT_TRUE(parsed.hasValue());
	const GptHeader& header = parsed.value();
	EXPECT_EQ(header.myLba, kValidSpec.myLba);
	EXPECT_EQ(header.alternateLba, kValidSpec.alternateLba);
	EXPECT_EQ(header.firstUsableLba, revenant::testing::kFixtureFirstStart);
	EXPECT_EQ(header.lastUsableLba, kValidSpec.lastUsableLba);
	EXPECT_EQ(header.entryArrayLba, kValidSpec.entryArrayLba);
	EXPECT_EQ(header.entryCount, kValidSpec.entryCount);
	EXPECT_EQ(header.entryBytes, kValidSpec.entryBytes);
	EXPECT_EQ(header.entryArrayCrc, kArrayCrc);
}

TEST(Gpt, ASpanShorterThanAHeaderIsOutOfRange) {
	const std::vector<std::byte> stub(50, std::byte{0});
	EXPECT_EQ(rejectionOf(stub), outOfRangeAt(stub.size()));
}

TEST(Gpt, ASectorThatDoesNotNameTheFormatIsRejected) {
	auto header = makeGptHeader(kValidSpec);
	header.at(0) = std::byte{'X'};
	signGptHeader(header);
	EXPECT_EQ(rejectionOf(header), invalidAt(0x00));
}

TEST(Gpt, AHeaderSizeBelowTheSpecifiedMinimumIsRejected) {
	EXPECT_EQ(rejectionOf(headerWith(0x0C, std::uint32_t{91})), invalidAt(0x0C));
}

TEST(Gpt, AHeaderSizeLargerThanWhatIsThereIsRejected) {
	EXPECT_EQ(rejectionOf(headerWith(0x0C, std::uint32_t{200})), invalidAt(0x0C));
}

// The checksum is the only evidence the rest of the header was written rather
// than landed there, so it is checked before any of it is believed.
TEST(Gpt, AHeaderWhoseChecksumDoesNotMatchIsRejected) {
	auto header = makeGptHeader(kValidSpec);
	writeLe(header, 0x28, std::uint64_t{7});
	EXPECT_EQ(rejectionOf(header), invalidAt(kCrcOffset));
}

// The two copies are byte-identical but for this field; believing one at the
// other's place would send the read to the other copy's entry array.
TEST(Gpt, AHeaderFoundSomewhereOtherThanWhereItSaysIsRejected) {
	const auto parsed = parseGptHeader(makeGptHeader(kValidSpec), 2);
	EXPECT_EQ(revenant::testing::rejectionOf(parsed), invalidAt(0x18));
}

TEST(Gpt, AFirstUsableSectorAboveTheLastIsRejected) {
	EXPECT_EQ(rejectionOf(headerWith(0x28, kValidSpec.lastUsableLba + 1)), invalidAt(0x28));
}

TEST(Gpt, AnEntrySizeBelowTheSpecifiedMinimumIsRejected) {
	EXPECT_EQ(rejectionOf(headerWith(0x54, std::uint32_t{64})), invalidAt(0x54));
}

TEST(Gpt, AnEntrySizeThatIsNotAMultipleOfEightIsRejected) {
	EXPECT_EQ(rejectionOf(headerWith(0x54, std::uint32_t{130})), invalidAt(0x54));
}

// The count and the size are both attacker-chosen, and their product would size
// the allocation that reads the array (ADR-0009).
TEST(Gpt, AnEntryArrayLargerThanTheCapIsRejected) {
	EXPECT_EQ(rejectionOf(headerWith(0x50, std::uint32_t{40000})), outOfRangeAt(0x50));
}

TEST(GptEntrySlot, ReadsTheTypeRangeAndNameOfAnEntry) {
	const auto parsed = parseGptEntry(makeUsedEntry("Data"));
	ASSERT_TRUE(parsed.hasValue());
	const GptEntry& entry = parsed.value();
	ASSERT_TRUE(std::ranges::equal(entry.typeGuid, gptTypeGuid(kTypeSeed)));
	EXPECT_EQ(entry.firstLba, 100U);
	EXPECT_EQ(entry.lastLba, 199U);
	EXPECT_EQ(entry.name, std::string{"Data"});
	EXPECT_TRUE(entry.nameIsExact);
}

TEST(GptEntrySlot, ASlotShorterThanAnEntryIsOutOfRange) {
	const std::vector<std::byte> stub(100, std::byte{0});
	EXPECT_EQ(revenant::testing::rejectionOf(parseGptEntry(stub)), outOfRangeAt(stub.size()));
}

TEST(GptEntrySlot, ALastSectorBelowTheFirstIsRejected) {
	auto slot = makeUsedEntry("Data");
	writeLe(slot, 0x28, std::uint64_t{99});
	EXPECT_EQ(revenant::testing::rejectionOf(parseGptEntry(slot)), invalidAt(0x28));
}

// The field holds 36 code units and carries no terminator when they are all
// used; trimming that stopped only at a NUL would read past the field.
TEST(GptEntrySlot, ANameFillingEveryCodeUnitDecodesWhole) {
	const std::string full(36, 'x');
	const auto parsed = parseGptEntry(makeUsedEntry(full));
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_EQ(parsed.value().name, full);
}

TEST(GptEntrySlot, AnUnusedSlotParsesAndIsRecognized) {
	const std::vector<std::byte> slot(kGptEntryBytes, std::byte{0});
	const auto parsed = parseGptEntry(slot);
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_TRUE(isUnusedEntry(parsed.value()));
}

TEST(GptEntrySlot, AUsedSlotIsNotUnused) {
	const auto parsed = parseGptEntry(makeUsedEntry("Data"));
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_FALSE(isUnusedEntry(parsed.value()));
}

} // namespace
