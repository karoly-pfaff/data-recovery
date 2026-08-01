// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The one encoding a block device is keyed by in this project
// (story-0609).
//
// Every comparison the destination rule makes runs between two of these, and
// they are never stored, shown, or carried across a run — so what the encoding
// *is* does not matter, and that both sides use the same one is the whole
// requirement. Writing it down once is not tidiness: a `dev_t` and a sysfs
// "major:minor" text are two spellings of one device, and packing them
// differently makes every comparison between them silently false.

#include <cstdint>
#include <optional>
#include <string_view>

namespace revenant {

// The high half of the key, and the only place that number is written.
inline constexpr unsigned int kDeviceMajorShift = 32;

[[nodiscard]] constexpr std::uint64_t deviceKey(std::uint64_t major, std::uint64_t minor) {
	return (major << kDeviceMajorShift) | minor;
}

// The same key out of a "major:minor" text, which is how sysfs and the mount
// table both spell a device. Nothing, when the text is not one.
[[nodiscard]] std::optional<std::uint64_t> deviceKeyIn(std::string_view text);

} // namespace revenant
