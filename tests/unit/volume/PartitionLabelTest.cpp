// SPDX-License-Identifier: GPL-3.0-or-later
// story-0045: the one line that lets a person recognize a partition as theirs.
// A label is a convenience and is allowed to be one — what these assert is that
// it never *invents* a name, and always falls back to something an operator can
// still look up.
#include "volume/PartitionLabel.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "revenant/volume/Gpt.hpp"
#include "revenant/volume/GptPartitions.hpp"

namespace {

using revenant::volume::GptPartition;
using revenant::volume::kGuidBytes;
using revenant::volume::labelOfGptPartition;
using revenant::volume::labelOfMbrType;

// Microsoft basic data, as it sits on disk: the first three fields
// little-endian, the last two not.
constexpr std::array<std::uint8_t, kGuidBytes> kBasicData{
	0xA2,
	0xA0,
	0xD0,
	0xEB,
	0xE5,
	0xB9,
	0x33,
	0x44,
	0x87,
	0xC0,
	0x68,
	0xB6,
	0xB7,
	0x26,
	0x99,
	0xC7};

[[nodiscard]] std::array<std::byte, kGuidBytes>
guidOf(const std::array<std::uint8_t, kGuidBytes>& raw) {
	std::array<std::byte, kGuidBytes> guid{};
	std::ranges::transform(raw, guid.begin(), [](std::uint8_t value) {
		return static_cast<std::byte>(value);
	});
	return guid;
}

TEST(PartitionLabel, NamesAWellKnownMbrType) {
	EXPECT_EQ(labelOfMbrType(0x07), std::string{"NTFS/exFAT"});
	EXPECT_EQ(labelOfMbrType(0x83), std::string{"Linux"});
}

// The list is short on purpose; anything outside it keeps its raw type, which
// an operator can still look up.
TEST(PartitionLabel, FallsBackToTheRawMbrTypeByte) {
	EXPECT_EQ(labelOfMbrType(0x42), std::string{"type 0x42"});
	EXPECT_EQ(labelOfMbrType(0x00), std::string{"type 0x00"});
}

// Built field by field rather than with a designated-initializer list: a list
// that names only some of an aggregate's fields is a warning on clang, and one
// that names all of them buries the two that matter here.
[[nodiscard]] GptPartition
partitionOfType(const std::array<std::uint8_t, kGuidBytes>& type, std::string_view name) {
	GptPartition partition;
	partition.typeGuid = guidOf(type);
	partition.name = name;
	return partition;
}

TEST(PartitionLabel, AGptPartitionsOwnNameWins) {
	EXPECT_EQ(labelOfGptPartition(partitionOfType(kBasicData, "Windows")), std::string{"Windows"});
}

TEST(PartitionLabel, AnUnnamedGptPartitionFallsBackToItsType) {
	EXPECT_EQ(
		labelOfGptPartition(partitionOfType(kBasicData, "")),
		std::string{"Windows basic data"});
}

TEST(PartitionLabel, AnUnnamedGptPartitionOfAnUnknownTypeSaysSo) {
	const GptPartition partition{};
	EXPECT_EQ(labelOfGptPartition(partition), std::string{"GPT partition"});
}

} // namespace
