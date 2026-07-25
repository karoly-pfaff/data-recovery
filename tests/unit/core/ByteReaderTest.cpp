// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/ByteReader.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "revenant/core/Error.hpp"

namespace {

using revenant::ByteReader;
using revenant::ErrorCode;

constexpr std::array<std::byte, 6> kData{std::byte{0x11},
                                         std::byte{0x22},
                                         std::byte{0x33},
                                         std::byte{0x44},
                                         std::byte{0x55},
                                         std::byte{0x66}};

TEST(ByteReader, ReadsSubrange) {
    const ByteReader reader{kData};
    const auto sub = reader.bytes(2, 3);
    ASSERT_TRUE(sub.hasValue());
    EXPECT_EQ(sub.value().front(), std::byte{0x33});
    EXPECT_EQ(sub.value().size(), 3U);
}

TEST(ByteReader, ReadPastEndIsTypedError) {
    const ByteReader reader{kData};
    EXPECT_EQ(reader.bytes(4, 3).error().code, ErrorCode::kOutOfRange);
}

TEST(ByteReader, ReadAtEndZeroCountIsEmpty) {
    const ByteReader reader{kData};
    EXPECT_EQ(reader.bytes(kData.size(), 0).value().size(), 0U);
}

TEST(ByteReader, OffsetBeyondEndIsTypedError) {
    const ByteReader reader{kData};
    EXPECT_EQ(reader.bytes(kData.size() + 1, 0).error().code, ErrorCode::kOutOfRange);
}

TEST(ByteReader, HugeOffsetIsTypedError) {
    const ByteReader reader{kData};
    const auto result = reader.bytes(std::numeric_limits<std::uint64_t>::max(), 1);
    EXPECT_EQ(result.error().code, ErrorCode::kOutOfRange);
}

TEST(ByteReader, ReadsLittleEndianInteger) {
    const ByteReader reader{kData};
    EXPECT_EQ(reader.readLe<std::uint32_t>(1).value(), 0x55443322U);
}

TEST(ByteReader, ReadsBigEndianInteger) {
    const ByteReader reader{kData};
    EXPECT_EQ(reader.readBe<std::uint16_t>(0).value(), 0x1122U);
}

TEST(ByteReader, IntegerReadCannotOverrunSpan) {
    const ByteReader reader{kData};
    EXPECT_EQ(reader.readLe<std::uint32_t>(3).error().code, ErrorCode::kOutOfRange);
}

} // namespace
