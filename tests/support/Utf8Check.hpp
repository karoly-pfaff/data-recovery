// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A small, sufficient (not exhaustive — no overlong-encoding or surrogate
// code-point rejection) UTF-8 well-formedness checker: every lead byte
// declares a valid length and every declared continuation byte is present and
// well-formed.
//
// Test-only, and shared by every fuzz target that decodes a name off a disk:
// "the output is always valid UTF-8" is one invariant (ADR-0010), so it is
// checked by one piece of code. Every function here is `inline`, so the header
// is safely includable from any translation unit.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace revenant::testing {

namespace utf8check {

inline std::uint8_t byteAt(std::string_view text, std::size_t index) {
	return static_cast<std::uint8_t>(text.at(index));
}

inline bool isContinuationByte(std::uint8_t byte) {
	return (byte & 0xC0U) == 0x80U;
}

// One recognized UTF-8 lead-byte shape: `value` is what `lead & mask` must
// equal for this shape to match, `continuationBytes` is how many follow.
struct LeadShape {
	std::uint8_t mask;
	std::uint8_t value;
	int continuationBytes;
};

inline constexpr std::array<LeadShape, 4> kLeadShapes{{
	{.mask = 0x80U, .value = 0x00U, .continuationBytes = 0}, // 0xxxxxxx - ASCII
	{.mask = 0xE0U, .value = 0xC0U, .continuationBytes = 1}, // 110xxxxx - 2-byte
	{.mask = 0xF0U, .value = 0xE0U, .continuationBytes = 2}, // 1110xxxx - 3-byte
	{.mask = 0xF8U, .value = 0xF0U, .continuationBytes = 3}, // 11110xxx - 4-byte
}};

// How many continuation bytes must follow this lead byte, or -1 if `lead`
// matches none of the recognized lead-byte shapes.
inline int continuationCount(std::uint8_t lead) {
	for (const LeadShape& shape : kLeadShapes) {
		if ((lead & shape.mask) == shape.value) {
			return shape.continuationBytes;
		}
	}
	return -1;
}

inline bool hasEnoughContinuationBytes(std::string_view text, std::size_t leadIndex, int count) {
	return leadIndex + static_cast<std::size_t>(count) < text.size();
}

// leadIndex/count come only from isValidUtf8's own loop, in this fixed order;
// no second call site exists for them to be swapped at.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline bool allContinuationBytesAreValid(std::string_view text, std::size_t leadIndex, int count) {
	for (int step = 1; step <= count; ++step) {
		if (!isContinuationByte(byteAt(text, leadIndex + static_cast<std::size_t>(step)))) {
			return false;
		}
	}
	return true;
}

inline bool hasValidContinuations(std::string_view text, std::size_t leadIndex, int count) {
	return hasEnoughContinuationBytes(text, leadIndex, count) &&
		   allContinuationBytesAreValid(text, leadIndex, count);
}

} // namespace utf8check

inline bool isValidUtf8(std::string_view text) {
	std::size_t index = 0;
	while (index < text.size()) {
		const int count = utf8check::continuationCount(utf8check::byteAt(text, index));
		if (count < 0 || !utf8check::hasValidContinuations(text, index, count)) {
			return false;
		}
		index += static_cast<std::size_t>(count) + 1;
	}
	return true;
}

} // namespace revenant::testing
