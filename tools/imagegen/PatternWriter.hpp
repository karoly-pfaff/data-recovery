// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

#include "revenant/core/Result.hpp"

namespace revenant::imagegen {

inline constexpr std::size_t kSectorBytes = 512;

// Deterministic fill patterns for synthetic test images.
enum class Pattern : std::uint8_t {
    kZero,    // all zero bytes
    kCounter, // byte j of sector n = (n*512 + j) & 0xFF — offsets self-describe
    kLbaTag,  // zeros, with the LE64 sector number stamped in bytes [0,8)
};

// Maps a CLI pattern name ("zero" | "counter" | "lba") to its enumerator.
[[nodiscard]] Result<Pattern> parsePattern(std::string_view name) noexcept;

// Fills one sector's bytes for sector number `lba`, deterministically.
void fillSector(std::span<std::byte> sector, std::uint64_t lba, Pattern pattern) noexcept;

// Writes `sizeBytes` of `pattern` to `outputPath`; returns bytes written.
[[nodiscard]] Result<std::uint64_t>
writeImage(const std::filesystem::path& outputPath, std::uint64_t sizeBytes, Pattern pattern);

} // namespace revenant::imagegen
