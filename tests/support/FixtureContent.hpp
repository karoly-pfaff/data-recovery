// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace revenant::testing {

// The two sides of a byte-identity check on a recovery: what the fixture put
// on the volume, and what came back out of the destination.

// Every byte of `path`, or nothing when it cannot be read.
[[nodiscard]] std::vector<std::byte> readFileBytes(const std::filesystem::path& path);

// The same, as text — for the documents a run leaves behind rather than the
// artifacts it recovered.
[[nodiscard]] std::string readFileText(const std::filesystem::path& path);

// The story-0065 fixture file called `name`, as the image builder wrote it.
[[nodiscard]] std::vector<std::byte> fixtureContentNamed(std::string_view name);

} // namespace revenant::testing
