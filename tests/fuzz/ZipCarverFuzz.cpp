// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: arbitrary bytes into ZipCarver::carve yield a verdict or a typed
// error — never a crash, hang, or OOB read (ASan-checked in CI).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "carve/formats/ZipCarver.hpp"
#include "revenant/core/ByteReader.hpp"

namespace {

// Copies the fuzzer-owned input into `std::byte` storage we can safely hold
// past this call — via span iterators, never raw pointer arithmetic.
std::vector<std::byte> toByteVector(std::span<const std::uint8_t> input) {
	std::vector<std::byte> bytes(input.size());
	std::ranges::transform(input, bytes.begin(), [](std::uint8_t b) { return std::byte{b}; });
	return bytes;
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const std::vector<std::byte> bytes = toByteVector(std::span<const std::uint8_t>{data, size});
	revenant::ByteReader reader{bytes};
	static_cast<void>(revenant::carve::ZipCarver{}.carve(reader));
	return 0;
}
