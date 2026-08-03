// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string_view>

#include "revenant/core/Result.hpp"

namespace revenant::imagegen {

inline constexpr std::size_t kSectorBytes = 512;

// Deterministic fill patterns for synthetic test images.
enum class Pattern : std::uint8_t {
	kZero, // all zero bytes
	// Byte j of sector n = (n*512 + j) & 0xFF. The sector term is a multiple of
	// 256, so this repeats every 256 bytes and every sector holds the same
	// bytes: an offset self-describes modulo 256, not absolutely. Anything
	// wanting to tell one sector from another wants kLbaTag.
	kCounter,
	kLbaTag, // zeros, with the LE64 sector number stamped in bytes [0,8)
};

// Maps a CLI pattern name ("zero" | "counter" | "lba") to its enumerator.
[[nodiscard]] Result<Pattern> parsePattern(std::string_view name) noexcept;

// Fills one sector's bytes for sector number `lba`, deterministically.
void fillSector(std::span<std::byte> sector, std::uint64_t lba, Pattern pattern) noexcept;

// Writes `pattern` over the device byte range [`from`, `to`) into an already
// open stream, one sector at a time; returns the offset it reached, which is
// short of `to` only when the stream failed. `from` must be sector-aligned, so
// the pattern stays a function of the device offset rather than of how much has
// been written — a builder that interleaves content with filler needs that, and
// `writeImage` below is this over a whole image.
[[nodiscard]] std::uint64_t
writeFiller(std::ostream& stream, std::uint64_t from, std::uint64_t to, Pattern pattern);

// Writes `sizeBytes` of `pattern` to `outputPath`; returns bytes written.
[[nodiscard]] Result<std::uint64_t>
writeImage(const std::filesystem::path& outputPath, std::uint64_t sizeBytes, Pattern pattern);

} // namespace revenant::imagegen
