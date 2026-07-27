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

// Writes that volume to `path`; returns the bytes written.
[[nodiscard]] Result<std::uint64_t> writeNtfsImage(const std::filesystem::path& path);

} // namespace revenant::imagegen::ntfs
