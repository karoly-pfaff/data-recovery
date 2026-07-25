// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/Endian.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

constexpr std::array<std::byte, 8> kSample{std::byte{0xEF},
                                           std::byte{0xCD},
                                           std::byte{0xAB},
                                           std::byte{0x89},
                                           std::byte{0x67},
                                           std::byte{0x45},
                                           std::byte{0x23},
                                           std::byte{0x01}};

TEST(Endian, ReadsLittleEndian8) {
    EXPECT_EQ(
        revenant::fromLittleEndian<std::uint8_t>(std::span<const std::byte, 1>{kSample.data(), 1}),
        0xEFU);
}

TEST(Endian, ReadsLittleEndian16) {
    EXPECT_EQ(
        revenant::fromLittleEndian<std::uint16_t>(std::span<const std::byte, 2>{kSample.data(), 2}),
        0xCDEFU);
}

TEST(Endian, ReadsLittleEndian32) {
    EXPECT_EQ(
        revenant::fromLittleEndian<std::uint32_t>(std::span<const std::byte, 4>{kSample.data(), 4}),
        0x89ABCDEFU);
}

TEST(Endian, ReadsLittleEndian64) {
    EXPECT_EQ(revenant::fromLittleEndian<std::uint64_t>(kSample), 0x0123456789ABCDEFULL);
}

TEST(Endian, ReadsBigEndian32) {
    EXPECT_EQ(
        revenant::fromBigEndian<std::uint32_t>(std::span<const std::byte, 4>{kSample.data(), 4}),
        0xEFCDAB89U);
}

TEST(Endian, ReadsBigEndian64) {
    EXPECT_EQ(revenant::fromBigEndian<std::uint64_t>(kSample), 0xEFCDAB8967452301ULL);
}

} // namespace
