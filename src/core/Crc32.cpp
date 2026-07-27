// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/Crc32.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace revenant {

namespace {

constexpr std::uint32_t kPolynomial = 0xEDB88320U;
constexpr std::uint32_t kInitial = 0xFFFFFFFFU;
constexpr std::size_t kTableSize = 256;
constexpr std::uint32_t kByteMask = 0xFFU;
constexpr std::uint32_t kBitsPerByte = 8;

[[nodiscard]] constexpr std::uint32_t tableEntry(std::uint32_t index) noexcept {
	std::uint32_t value = index;
	for (std::uint32_t bit = 0; bit < kBitsPerByte; ++bit) {
		value = ((value & 1U) != 0U) ? (kPolynomial ^ (value >> 1U)) : (value >> 1U);
	}
	return value;
}

[[nodiscard]] constexpr std::array<std::uint32_t, kTableSize> makeTable() noexcept {
	std::array<std::uint32_t, kTableSize> table{};
	for (std::uint32_t index = 0; index < kTableSize; ++index) {
		table.at(index) = tableEntry(index);
	}
	return table;
}

constexpr auto kTable = makeTable();

} // namespace

std::uint32_t crc32(std::span<const std::byte> data) noexcept {
	std::uint32_t crc = kInitial;
	for (const std::byte value : data) {
		const auto index = (crc ^ std::to_integer<std::uint32_t>(value)) & kByteMask;
		crc = kTable.at(index) ^ (crc >> kBitsPerByte);
	}
	return crc ^ kInitial;
}

} // namespace revenant
