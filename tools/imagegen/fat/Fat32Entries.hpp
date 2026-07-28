// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace revenant::imagegen::fat {

inline constexpr std::size_t kSlotBytes = 32;

// One directory slot as the fixture wants it: an 8.3 name, an attribute byte,
// where the content starts and how long it is. `deleted` stamps the `0xE5`
// marker over the name's first byte, exactly as a deletion does.
struct SlotSpec {
	std::string_view shortName; // 11 bytes, space-padded
	std::uint8_t attributes;
	// The `NTRes` case flags: bit 3 lower-cases the base name and bit 4 the
	// extension. An 8.3 field is always stored upper, so this is the only
	// record of the case a name was created with.
	std::uint8_t caseFlags;
	std::uint32_t firstCluster;
	std::uint32_t sizeInBytes;
	bool deleted;
};

inline constexpr std::uint8_t kLowerCaseBase = 0x08;

inline constexpr std::uint8_t kAttrArchive = 0x20;
inline constexpr std::uint8_t kAttrDirectory = 0x10;
inline constexpr std::uint8_t kAttrLongName = 0x0F;

// The 32 bytes of a short entry.
[[nodiscard]] std::vector<std::byte> shortSlot(const SlotSpec& spec);

// The 32 bytes of one long-name fragment: `ordinal` already carries the
// last-fragment flag where it belongs, and `text` is the fragment's share of
// the name.
[[nodiscard]] std::vector<std::byte>
longNameSlot(std::uint8_t ordinal, std::string_view text, std::uint8_t checksum);

// The checksum a long-name set carries, computed the way the format defines it
// over the 11 raw bytes of the short name it belongs to.
[[nodiscard]] std::uint8_t shortNameChecksum(std::string_view shortName);

} // namespace revenant::imagegen::fat
