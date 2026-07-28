// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/fat/Fat32Entries.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "imagegen/ByteWriter.hpp"

namespace revenant::imagegen::fat {

namespace {

constexpr std::size_t kNameBytes = 11;
constexpr std::byte kDeletedMarker{0xE5};

// A created and written stamp of 2020-08-01 12:00:00, so every fixture entry
// carries a timestamp a test can name.
constexpr std::uint16_t kFixtureDate = ((2020U - 1980U) << 9U) | (8U << 5U) | 1U;
constexpr std::uint16_t kFixtureTime = 12U << 11U;

// The three runs a fragment splits its 13 code units across.
constexpr std::array<std::size_t, 3> kRunOffsets{0x01, 0x0E, 0x1C};
constexpr std::array<std::size_t, 3> kRunUnits{5, 6, 2};

void putName(std::vector<std::byte>& slot, std::string_view shortName) {
	for (std::size_t at = 0; at < kNameBytes; ++at) {
		slot.at(at) =
			at < shortName.size() ? static_cast<std::byte>(shortName.at(at)) : std::byte{0x20};
	}
}

// One code unit of the long name, or the padding that follows its terminator.
[[nodiscard]] std::uint16_t unitAt(std::string_view text, std::size_t index) {
	if (index < text.size()) {
		return static_cast<std::uint16_t>(text.at(index));
	}
	return index == text.size() ? 0x0000U : 0xFFFFU;
}

// How many code units the runs before this one already carried.
[[nodiscard]] std::size_t unitsBefore(std::size_t run) {
	std::size_t units = 0;
	for (std::size_t before = 0; before < run; ++before) {
		units += kRunUnits.at(before);
	}
	return units;
}

void putRun(std::vector<std::byte>& slot, std::size_t run, std::string_view text) {
	const auto index = unitsBefore(run);
	for (std::size_t unit = 0; unit < kRunUnits.at(run); ++unit) {
		putLe<std::uint16_t>(slot, kRunOffsets.at(run) + (unit * 2), unitAt(text, index + unit));
	}
}

void putAttributes(std::vector<std::byte>& slot, const SlotSpec& spec) {
	putLe<std::uint8_t>(slot, 0x0B, spec.attributes);
	putLe<std::uint8_t>(slot, 0x0C, spec.caseFlags);
}

// Created, accessed and written all land on the one fixture stamp, so a test
// naming any of them names the same instant.
void putStamps(std::vector<std::byte>& slot) {
	putLe<std::uint16_t>(slot, 0x0E, kFixtureTime);
	putLe<std::uint16_t>(slot, 0x10, kFixtureDate);
	putLe<std::uint16_t>(slot, 0x12, kFixtureDate);
	putLe<std::uint16_t>(slot, 0x16, kFixtureTime);
	putLe<std::uint16_t>(slot, 0x18, kFixtureDate);
}

// FAT splits the cluster number across two fields, sixteen bits apart.
void putContentFields(std::vector<std::byte>& slot, const SlotSpec& spec) {
	putLe<std::uint16_t>(slot, 0x14, static_cast<std::uint16_t>(spec.firstCluster >> 16U));
	putLe<std::uint16_t>(slot, 0x1A, static_cast<std::uint16_t>(spec.firstCluster & 0xFFFFU));
	putLe<std::uint32_t>(slot, 0x1C, spec.sizeInBytes);
}

} // namespace

std::vector<std::byte> shortSlot(const SlotSpec& spec) {
	std::vector<std::byte> slot(kSlotBytes, std::byte{0});
	putName(slot, spec.shortName);
	putAttributes(slot, spec);
	putStamps(slot);
	putContentFields(slot, spec);
	if (spec.deleted) {
		slot.at(0) = kDeletedMarker;
	}
	return slot;
}

std::vector<std::byte>
longNameSlot(std::uint8_t ordinal, std::string_view text, std::uint8_t checksum) {
	std::vector<std::byte> slot(kSlotBytes, std::byte{0});
	putLe<std::uint8_t>(slot, 0x00, ordinal);
	putLe<std::uint8_t>(slot, 0x0B, kAttrLongName);
	putLe<std::uint8_t>(slot, 0x0D, checksum);
	for (std::size_t run = 0; run < kRunOffsets.size(); ++run) {
		putRun(slot, run, text);
	}
	return slot;
}

std::uint8_t shortNameChecksum(std::string_view shortName) {
	std::uint8_t sum = 0;
	for (std::size_t at = 0; at < kNameBytes; ++at) {
		const auto byte =
			at < shortName.size() ? static_cast<std::uint8_t>(shortName.at(at)) : 0x20U;
		sum = static_cast<std::uint8_t>(((sum & 1U) << 7U) + (sum >> 1U) + byte);
	}
	return sum;
}

} // namespace revenant::imagegen::fat
