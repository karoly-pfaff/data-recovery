// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: arbitrary bytes as raw UTF-16LE input, run through
// decodeUtf16Name, ALWAYS produce a valid UTF-8 string — never a crash,
// hang, or malformed sequence (ADR-0010). No project assertion macro exists
// to depend on (see OutputPathFuzz.cpp's note); a bare `std::abort()` is
// used deliberately so libFuzzer sees an unambiguous crash and keeps the
// triggering input in its crash corpus.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>
#include <vector>

#include "revenant/fs/NameDecode.hpp"

namespace {

std::uint8_t byteAt(std::string_view text, std::size_t index) {
	return static_cast<std::uint8_t>(text.at(index));
}

bool isContinuationByte(std::uint8_t byte) {
	return (byte & 0xC0U) == 0x80U;
}

// One recognized UTF-8 lead-byte shape: `value` is what `lead & mask` must
// equal for this shape to match, `continuationBytes` is how many follow.
struct LeadShape {
	std::uint8_t mask;
	std::uint8_t value;
	int continuationBytes;
};

constexpr std::array<LeadShape, 4> kLeadShapes{{
	{.mask = 0x80U, .value = 0x00U, .continuationBytes = 0}, // 0xxxxxxx - ASCII
	{.mask = 0xE0U, .value = 0xC0U, .continuationBytes = 1}, // 110xxxxx - 2-byte
	{.mask = 0xF0U, .value = 0xE0U, .continuationBytes = 2}, // 1110xxxx - 3-byte
	{.mask = 0xF8U, .value = 0xF0U, .continuationBytes = 3}, // 11110xxx - 4-byte
}};

// How many continuation bytes must follow this lead byte, or -1 if `lead`
// matches none of the recognized lead-byte shapes.
int continuationCount(std::uint8_t lead) {
	for (const LeadShape& shape : kLeadShapes) {
		if ((lead & shape.mask) == shape.value) {
			return shape.continuationBytes;
		}
	}
	return -1;
}

bool hasEnoughContinuationBytes(std::string_view text, std::size_t leadIndex, int count) {
	return leadIndex + static_cast<std::size_t>(count) < text.size();
}

// leadIndex/count come only from isValidUtf8's own loop, in this fixed
// order; no second call site exists for them to be swapped at.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool allContinuationBytesAreValid(std::string_view text, std::size_t leadIndex, int count) {
	for (int step = 1; step <= count; ++step) {
		if (!isContinuationByte(byteAt(text, leadIndex + static_cast<std::size_t>(step)))) {
			return false;
		}
	}
	return true;
}

bool hasValidContinuations(std::string_view text, std::size_t leadIndex, int count) {
	return hasEnoughContinuationBytes(text, leadIndex, count) &&
		   allContinuationBytesAreValid(text, leadIndex, count);
}

// A small, sufficient (not exhaustive - no overlong-encoding or surrogate
// code-point rejection) UTF-8 well-formedness checker: every lead byte
// declares a valid length and every declared continuation byte is present
// and well-formed.
bool isValidUtf8(std::string_view text) {
	std::size_t index = 0;
	while (index < text.size()) {
		const int count = continuationCount(byteAt(text, index));
		if (count < 0 || !hasValidContinuations(text, index, count)) {
			return false;
		}
		index += static_cast<std::size_t>(count) + 1;
	}
	return true;
}

// Copies the fuzzer-owned input into `std::byte` storage we can safely hold
// past this call — via span iterators, never raw pointer arithmetic.
std::vector<std::byte> toByteVector(std::span<const std::uint8_t> input) {
	std::vector<std::byte> bytes(input.size());
	std::ranges::transform(input, bytes.begin(), [](std::uint8_t byte) { return std::byte{byte}; });
	return bytes;
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const std::vector<std::byte> bytes = toByteVector(std::span<const std::uint8_t>{data, size});
	const auto decoded = revenant::fs::decodeUtf16Name(bytes);
	if (!isValidUtf8(decoded.utf8)) {
		std::abort();
	}
	return 0;
}
