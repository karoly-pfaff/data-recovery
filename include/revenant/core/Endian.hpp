// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <span>

namespace revenant {

namespace detail {

// Byte-reverses an unsigned integer with pure shift arithmetic — constexpr,
// no UB, no compiler intrinsics required (std::byteswap is C++23).
template <std::unsigned_integral T> [[nodiscard]] constexpr T byteSwap(T value) noexcept {
	T result = 0;
	for (std::size_t i = 0; i < sizeof(T); ++i) {
		result = static_cast<T>(static_cast<T>(result << 8U) | (value & T{0xFF}));
		// For T = uint8_t this shift's amount (8) equals the type's own
		// width, which MSVC statically flags as data loss (C4333) even
		// though the shifted value is never read again. `if constexpr`
		// discards the shift for that one-byte case at compile time
		// instead of merely skipping it at runtime; output is unchanged.
		if constexpr (sizeof(T) > 1) {
			value = static_cast<T>(value >> 8U);
		}
	}
	return result;
}

// bit_cast the raw bytes to a native-endian T (no unaligned dereference).
template <std::unsigned_integral T>
[[nodiscard]] T nativeFromBytes(std::span<const std::byte, sizeof(T)> raw) noexcept {
	std::array<std::byte, sizeof(T)> bytes{};
	std::ranges::copy(raw, bytes.begin());
	return std::bit_cast<T>(bytes);
}

} // namespace detail

// Reads a fixed-width unsigned integer stored little-endian in `raw`.
template <std::unsigned_integral T>
[[nodiscard]] T fromLittleEndian(std::span<const std::byte, sizeof(T)> raw) noexcept {
	const T native = detail::nativeFromBytes<T>(raw);
	return std::endian::native == std::endian::little ? native : detail::byteSwap(native);
}

// Reads a fixed-width unsigned integer stored big-endian in `raw`.
template <std::unsigned_integral T>
[[nodiscard]] T fromBigEndian(std::span<const std::byte, sizeof(T)> raw) noexcept {
	const T native = detail::nativeFromBytes<T>(raw);
	return std::endian::native == std::endian::big ? native : detail::byteSwap(native);
}

// Writes a fixed-width unsigned integer as little-endian bytes (inverse of
// fromLittleEndian).
template <std::unsigned_integral T>
[[nodiscard]] std::array<std::byte, sizeof(T)> toLittleEndian(T value) noexcept {
	const T stored = std::endian::native == std::endian::little ? value : detail::byteSwap(value);
	return std::bit_cast<std::array<std::byte, sizeof(T)>>(stored);
}

} // namespace revenant
