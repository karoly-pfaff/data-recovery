// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Where the fields of a GPT header and of one partition entry sit.
// Shared by the parsers that validate them and by the reader that names the
// field it refuses. Not a public interface.

#include <array>
#include <cstddef>
#include <cstdint>

namespace revenant::volume {

// --- Header ------------------------------------------------------------------

inline constexpr std::uint64_t kSignatureOffset = 0x00;
inline constexpr std::uint64_t kHeaderSizeOffset = 0x0C;
inline constexpr std::uint64_t kHeaderCrcOffset = 0x10;
inline constexpr std::uint64_t kMyLbaOffset = 0x18;
inline constexpr std::uint64_t kAlternateLbaOffset = 0x20;
inline constexpr std::uint64_t kFirstUsableOffset = 0x28;
inline constexpr std::uint64_t kLastUsableOffset = 0x30;
inline constexpr std::uint64_t kEntryArrayLbaOffset = 0x48;
inline constexpr std::uint64_t kEntryCountOffset = 0x50;
inline constexpr std::uint64_t kEntryBytesOffset = 0x54;
inline constexpr std::uint64_t kEntryArrayCrcOffset = 0x58;

// The eight bytes that name the format. Spelled out rather than written as a
// string literal so that no terminator is implied.
inline constexpr std::array<std::byte, 8> kGptSignature{
	std::byte{'E'},
	std::byte{'F'},
	std::byte{'I'},
	std::byte{' '},
	std::byte{'P'},
	std::byte{'A'},
	std::byte{'R'},
	std::byte{'T'}};

// --- Entry -------------------------------------------------------------------

inline constexpr std::uint64_t kTypeGuidOffset = 0x00;
inline constexpr std::uint64_t kEntryFirstLbaOffset = 0x20;
inline constexpr std::uint64_t kEntryLastLbaOffset = 0x28;
inline constexpr std::uint64_t kEntryNameOffset = 0x38;

// 36 UTF-16 code units, NUL-padded.
inline constexpr std::size_t kEntryNameBytes = 72;

// An entry's size must be a multiple of this, and at least kGptEntryBytes.
inline constexpr std::uint32_t kEntrySizeGranularity = 8;

} // namespace revenant::volume
