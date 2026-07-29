// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

#include "revenant/core/Result.hpp"

namespace revenant::imagegen {

// Writes a whole image that was built in memory to `path`; returns the bytes
// written. Every builder that assembles its volume as one buffer ends here,
// so the ofstream, the truncation and the failure check are written once.
[[nodiscard]] Result<std::uint64_t>
writeImageBytes(const std::filesystem::path& path, std::span<const std::byte> image);

} // namespace revenant::imagegen
