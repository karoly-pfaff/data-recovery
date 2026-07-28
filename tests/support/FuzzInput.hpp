// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace revenant::testing {

// Copies the fuzzer-owned input into `std::byte` storage a target can safely
// hold past the call — via span iterators, never raw pointer arithmetic.
[[nodiscard]] inline std::vector<std::byte> toByteVector(std::span<const std::uint8_t> input) {
	std::vector<std::byte> bytes(input.size());
	std::ranges::transform(input, bytes.begin(), [](std::uint8_t value) {
		return std::byte{value};
	});
	return bytes;
}

} // namespace revenant::testing
