// SPDX-License-Identifier: GPL-3.0-or-later
#include "AsciiText.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <span>
#include <string>

namespace revenant::carve {

std::string asciiText(std::span<const std::byte> raw) {
	std::string text;
	text.reserve(raw.size());
	std::ranges::transform(raw, std::back_inserter(text), [](std::byte value) {
		return static_cast<char>(value);
	});
	return text;
}

} // namespace revenant::carve
