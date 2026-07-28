// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant under fuzzing: hashing bytes in one call and in two calls split
// anywhere must produce the same digest. A buffered hash gets this wrong at
// exactly the block and padding boundaries a fuzzer finds first.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

#include "revenant/core/Sha256.hpp"

namespace {

std::vector<std::byte> toByteVector(std::span<const std::uint8_t> input) {
	std::vector<std::byte> bytes(input.size());
	std::ranges::transform(input, bytes.begin(), [](std::uint8_t value) {
		return std::byte{value};
	});
	return bytes;
}

[[nodiscard]] revenant::Sha256Digest
hashInTwoParts(std::span<const std::byte> data, std::size_t at) {
	revenant::Sha256 hash;
	hash.update(data.first(at));
	hash.update(data.subspan(at));
	return hash.finish();
}

// The split point comes from the data itself, so the fuzzer can drive it.
[[nodiscard]] std::size_t splitPoint(std::span<const std::byte> data) {
	if (data.empty()) {
		return 0;
	}
	return std::to_integer<std::size_t>(data.front()) % (data.size() + 1);
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const auto bytes = toByteVector(std::span<const std::uint8_t>{data, size});
	const auto whole = revenant::sha256(bytes);
	const auto parts = hashInTwoParts(bytes, splitPoint(bytes));
	if (whole != parts) {
		// Aborting rather than returning: libFuzzer reserves the return value,
		// so a violated invariant has to fail loudly to be a finding at all.
		std::abort();
	}
	return 0;
}
