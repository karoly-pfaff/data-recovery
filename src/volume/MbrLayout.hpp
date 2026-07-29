// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Where the fields of a 512-byte partition table sit. Shared by the
// parser that validates them and by the reader that has to name the slot it
// refuses, so that neither restates the layout the other assumes. Not a public
// interface.

#include <cstddef>
#include <cstdint>

namespace revenant::volume {

inline constexpr std::uint64_t kTableOffset = 0x1BE;
inline constexpr std::uint64_t kEntryBytes = 16;
inline constexpr std::uint64_t kSignatureOffset = 0x1FE;
inline constexpr std::uint16_t kTableSignature = 0xAA55;

// Where each read field sits inside a 16-byte slot. The gaps between them are
// the two CHS triples, which nothing reads.
inline constexpr std::uint64_t kStatusField = 0x00;
inline constexpr std::uint64_t kTypeField = 0x04;
inline constexpr std::uint64_t kStartLbaField = 0x08;
inline constexpr std::uint64_t kSectorCountField = 0x0C;

// Where slot `index`'s bytes begin inside the sector.
[[nodiscard]] constexpr std::uint64_t slotOffset(std::size_t index) {
	return kTableOffset + (index * kEntryBytes);
}

} // namespace revenant::volume
