// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The directory-entry layout constants, shared by the slot
// classifier, the short-entry parser and the long-name parser. Not a public
// interface.

#include <cstddef>
#include <cstdint>

namespace revenant::fs::fat {

// Field offsets inside a 32-byte slot.
inline constexpr std::size_t kNameOffset = 0x00;
inline constexpr std::size_t kNameBytes = 11;
inline constexpr std::size_t kAttributesOffset = 0x0B;
inline constexpr std::size_t kCaseFlagsOffset = 0x0C;
inline constexpr std::size_t kCreatedTimeOffset = 0x0E;
inline constexpr std::size_t kCreatedDateOffset = 0x10;
inline constexpr std::size_t kAccessedDateOffset = 0x12;
inline constexpr std::size_t kClusterHighOffset = 0x14;
inline constexpr std::size_t kWrittenTimeOffset = 0x16;
inline constexpr std::size_t kWrittenDateOffset = 0x18;
inline constexpr std::size_t kClusterLowOffset = 0x1A;
inline constexpr std::size_t kSizeOffset = 0x1C;

// Attribute bits.
inline constexpr std::uint8_t kAttrVolumeLabel = 0x08;
inline constexpr std::uint8_t kAttrDirectory = 0x10;
// A long-name fragment sets read-only, hidden, system and volume-label at
// once — a combination no real file carries, which is exactly why it was
// chosen: an old DOS that did not know about long names skipped these slots.
inline constexpr std::uint8_t kAttrLongName = 0x0F;
inline constexpr std::uint8_t kAttrKindMask = 0x3F;

// First-byte markers.
inline constexpr std::uint8_t kEndOfDirectoryMarker = 0x00;
inline constexpr std::uint8_t kDeletedMarker = 0xE5;

} // namespace revenant::fs::fat
