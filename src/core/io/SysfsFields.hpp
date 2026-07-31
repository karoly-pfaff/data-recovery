// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Reading one sysfs attribute: the small file reads and the text
// conventions a node walk is built out of (story-0609). Deciding what a node
// *is* from them is SysfsWalk's job.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "revenant/core/Result.hpp"

namespace revenant {

// The first line of a sysfs attribute, which is all any of them hold.
[[nodiscard]] std::optional<std::string> sysfsLine(const std::filesystem::path& file);

// A `dev` attribute's "major:minor", as the one number a disk is compared by.
// Both halves are kept; the encoding matters only in being the same on both
// sides of that comparison.
[[nodiscard]] std::optional<std::uint64_t> sysfsDeviceNumber(std::string_view text);

// The same text, as the name the kernel's flat index files that device under.
[[nodiscard]] std::optional<std::string> sysfsNodeName(std::string_view text);

// A whole number held in an attribute.
[[nodiscard]] std::optional<std::uint64_t> sysfsNumber(const std::filesystem::path& file);

// sysfs states `start` and `size` in 512-byte units whatever the device's own
// sector size is — a kernel ABI, not a property of the disk. Nothing to convert
// is a failure, not a zero.
[[nodiscard]] Result<std::uint64_t> sysfsUnitsToBytes(std::optional<std::uint64_t> units);

// Whether a path is there at all, without throwing over one that is not.
[[nodiscard]] bool sysfsPresent(const std::filesystem::path& path);

} // namespace revenant
