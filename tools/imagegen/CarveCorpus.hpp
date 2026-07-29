// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "revenant/core/Result.hpp"

namespace revenant::imagegen {

// A volume with no filesystem and a carve header every few kilobytes: what the
// `carve-validate` benchmark needs, where the cost under test is validation
// rather than searching. One repeating unit holds a JPEG the carver can vouch
// for and a PNG signature with no chunk behind it, so a scan pays for a
// candidate it accepts and one it rejects in equal number.
inline constexpr std::size_t kCorpusJpegBytes = 4096;
inline constexpr std::size_t kCorpusRejectBytes = 4096;
inline constexpr std::size_t kCorpusUnitBytes = kCorpusJpegBytes + kCorpusRejectBytes;

// `sizeBytes` of that corpus, cut mid-unit if the size does not divide.
[[nodiscard]] std::vector<std::byte> buildCarveCorpus(std::size_t sizeBytes);

// The same, written to `path`; returns the bytes written.
[[nodiscard]] Result<std::uint64_t>
writeCarveCorpus(const std::filesystem::path& path, std::uint64_t sizeBytes);

} // namespace revenant::imagegen
