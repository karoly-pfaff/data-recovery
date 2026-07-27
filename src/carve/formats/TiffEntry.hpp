// SPDX-License-Identifier: GPL-3.0-or-later
// Internal. TIFF field decoding behind the RAW carver: byte-order-aware reads,
// IFD entries, and the values they point at. Not a public interface.
#pragma once

#include <cstdint>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

// A TIFF file's bytes together with the byte order every field in it is read
// in. The two are inseparable: reading a TIFF without its order is nonsense.
struct TiffContext {
	ByteReader reader; // by value: a span-sized view, cheap to copy
	bool bigEndian = false;
};

// One 12-byte IFD entry.
struct TiffEntry {
	std::uint16_t tag = 0;
	std::uint16_t type = 0;
	std::uint32_t count = 0;
	std::uint32_t valueOffset = 0; // or the value itself when it fits in 4 bytes
};

// Tags that locate the image data — the part that makes the extent exact.
inline constexpr std::uint16_t kMakeTag = 0x010F;
inline constexpr std::uint16_t kStripOffsetsTag = 0x0111;
inline constexpr std::uint16_t kStripByteCountsTag = 0x0117;
inline constexpr std::uint16_t kTileOffsetsTag = 0x0144;
inline constexpr std::uint16_t kTileByteCountsTag = 0x0145;

[[nodiscard]] Result<std::uint16_t> readU16(const TiffContext& tiff, std::uint64_t offset);
[[nodiscard]] Result<std::uint32_t> readU32(const TiffContext& tiff, std::uint64_t offset);

[[nodiscard]] Result<TiffEntry> readEntry(const TiffContext& tiff, std::uint64_t offset);

// How many bytes this entry's value occupies. Zero for a type this decoder
// does not know, which makes the entry unusable rather than guessed at.
[[nodiscard]] std::uint64_t valueBytes(const TiffEntry& entry);

// Where the entry's value ends, or 0 when it sits inline in the entry itself
// and so contributes nothing beyond the IFD table.
[[nodiscard]] std::uint64_t valueEnd(const TiffEntry& entry);

// The `index`-th element of a SHORT or LONG array entry, wherever it lives.
[[nodiscard]] Result<std::uint32_t>
arrayElement(const TiffContext& tiff, const TiffEntry& entry, std::uint32_t index);

} // namespace revenant::carve
