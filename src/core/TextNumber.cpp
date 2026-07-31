// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TextNumber.hpp"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace revenant {

// std::from_chars's [first, last) pointer pair is the only portable overload,
// and a string_view offers no iterator pair it accepts.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
std::optional<std::uint64_t> numberIn(std::string_view text, int base) {
	std::uint64_t value = 0;
	const auto* first = text.data();
	if (std::from_chars(first, first + text.size(), value, base).ec != std::errc{}) {
		return std::nullopt;
	}
	return value;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

} // namespace revenant
