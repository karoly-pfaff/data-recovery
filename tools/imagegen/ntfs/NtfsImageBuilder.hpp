// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "revenant/core/Result.hpp"

namespace revenant::imagegen::ntfs {

// Builds the whole fixture volume in memory: boot sector, `$MFT`, file data in
// its clusters, and one JPEG in unallocated space. Identical every run.
[[nodiscard]] std::vector<std::byte> buildNtfsImage();

// The same volume with `mftRecordCount` records — the fixed fixture's own
// seven files, padded with resident filler files. The `ntfs-enumerate`
// benchmark asks for a large one; everything else takes `buildNtfsImage`.
[[nodiscard]] std::vector<std::byte> buildNtfsImageWithRecords(std::uint32_t mftRecordCount);

// Writes that volume to `path`; returns the bytes written.
[[nodiscard]] Result<std::uint64_t>
writeNtfsImage(const std::filesystem::path& path, std::uint32_t mftRecordCount);

} // namespace revenant::imagegen::ntfs
