// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/NameEscape.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace revenant {

namespace {

char hexDigit(unsigned nibble) {
	constexpr std::array<char, 16>
		kHexDigits{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
	return kHexDigits.at(nibble & 0xFU);
}

} // namespace

void appendEscapedByte(std::string& out, std::byte raw) {
	const auto value = std::to_integer<unsigned>(raw);
	out += "%";
	out.push_back(hexDigit(value >> 4U));
	out.push_back(hexDigit(value));
}

void appendEscapedCodeUnit(std::string& out, std::uint16_t unit) {
	out += "%u";
	out.push_back(hexDigit(unit >> 12U));
	out.push_back(hexDigit(unit >> 8U));
	out.push_back(hexDigit(unit >> 4U));
	out.push_back(hexDigit(unit));
}

} // namespace revenant
