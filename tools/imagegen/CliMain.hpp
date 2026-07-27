// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

namespace revenant::imagegen {

// Parses one of
//   revenant-imagegen pattern <output> <size-bytes> <zero|counter|lba>
//   revenant-imagegen ntfs <output>
// and generates the image. Returns false (after logging to stderr) on any
// usage or write error.
[[nodiscard]] bool runCli(std::span<char* const> args);

} // namespace revenant::imagegen
