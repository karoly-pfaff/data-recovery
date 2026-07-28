// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: arbitrary bytes as raw UTF-16LE input, run through
// decodeUtf16Name, ALWAYS produce a valid UTF-8 string — never a crash,
// hang, or malformed sequence (ADR-0010). No project assertion macro exists
// to depend on (see OutputPathFuzz.cpp's note); a bare `std::abort()` is
// used deliberately so libFuzzer sees an unambiguous crash and keeps the
// triggering input in its crash corpus.
//
// The UTF-8 checker itself lives in tests/support/Utf8Check.hpp: the same
// invariant is asserted by every fuzz target that decodes a name off a disk,
// so it is written once.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

#include "revenant/fs/NameDecode.hpp"
#include "support/Utf8Check.hpp"

namespace {

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
	if (!revenant::testing::isValidUtf8(decoded.utf8)) {
		std::abort();
	}
	return 0;
}
