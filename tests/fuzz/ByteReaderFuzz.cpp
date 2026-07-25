// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant under fuzzing: any (offset, payload) produces a value or a typed
// error — never a crash, hang, or out-of-bounds access (ASan-checked).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/ByteReader.hpp"

namespace {

constexpr std::size_t kHeaderBytes = 8;

// Copies the fuzzer-owned input into `std::byte` storage we can safely hold
// past this call — via span iterators, never raw pointer arithmetic.
std::vector<std::byte> toByteVector(std::span<const std::uint8_t> input) {
    std::vector<std::byte> bytes(input.size());
    std::ranges::transform(input, bytes.begin(), [](std::uint8_t b) { return std::byte{b}; });
    return bytes;
}

void exercise(const revenant::ByteReader& reader, std::uint64_t offset) {
    static_cast<void>(reader.bytes(offset, 1));
    static_cast<void>(reader.bytes(offset, reader.size()));
    static_cast<void>(reader.readLe<std::uint16_t>(offset));
    static_cast<void>(reader.readLe<std::uint64_t>(offset));
    static_cast<void>(reader.readBe<std::uint32_t>(offset));
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < kHeaderBytes) {
        return 0;
    }
    const std::vector<std::byte> bytes = toByteVector(std::span<const std::uint8_t>{data, size});
    const revenant::ByteReader header{std::span<const std::byte>{bytes}.first(kHeaderBytes)};
    const std::uint64_t offset = header.readLe<std::uint64_t>(0).value();
    const revenant::ByteReader reader{std::span<const std::byte>{bytes}.subspan(kHeaderBytes)};
    exercise(reader, offset);
    return 0;
}
