// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Endian.hpp"
#include "revenant/core/Result.hpp"

namespace revenant {

// Bounds-checked reading over a borrowed, immutable byte range. Every
// out-of-range access is a typed error; no read can overrun the span.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::byte> data) noexcept : data_(data) {}

    [[nodiscard]] std::size_t size() const noexcept {
        return data_.size();
    }

    // A `count`-byte sub-view at `offset`, or kOutOfRange.
    [[nodiscard]] Result<std::span<const std::byte>> bytes(std::uint64_t offset,
                                                           std::size_t count) const noexcept;

    template <std::unsigned_integral T>
    [[nodiscard]] Result<T> readLe(std::uint64_t offset) const noexcept {
        return bytes(offset, sizeof(T)).map([](std::span<const std::byte> raw) {
            return fromLittleEndian<T>(raw.first<sizeof(T)>());
        });
    }

    template <std::unsigned_integral T>
    [[nodiscard]] Result<T> readBe(std::uint64_t offset) const noexcept {
        return bytes(offset, sizeof(T)).map([](std::span<const std::byte> raw) {
            return fromBigEndian<T>(raw.first<sizeof(T)>());
        });
    }

private:
    std::span<const std::byte> data_;
};

} // namespace revenant
