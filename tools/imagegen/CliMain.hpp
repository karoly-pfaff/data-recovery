// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

namespace revenant::imagegen {

// Parses `revenant-imagegen <output> <size-bytes> <pattern>` and generates the
// image. Returns false (after logging to stderr) on any usage or write error.
[[nodiscard]] bool runCli(std::span<char* const> args);

} // namespace revenant::imagegen
