// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstddef>
#include <span>
#include <vector>

#include "revenant/core/Endian.hpp"

namespace revenant::imagegen {

// Stamps `value` little-endian at `offset`. Shared by every filesystem's
// image builder. The builders write into buffers
// they sized themselves, so `.at()` here is not error handling — it is the
// assertion that turns a layout mistake in this tool into a loud throw instead
// of a corrupt fixture nobody notices until a parser test fails oddly.
template <std::unsigned_integral T>
void putLe(std::vector<std::byte>& target, std::size_t offset, T value) {
	const auto raw = toLittleEndian<T>(value);
	for (std::size_t i = 0; i < raw.size(); ++i) {
		target.at(offset + i) = raw.at(i);
	}
}

// The same, for a literal byte sequence (signatures, UTF-16 names, payloads).
// Iterated rather than indexed: std::span has no checked accessor in C++20,
// and the destination index stays `.at()`-checked either way.
inline void
putBytes(std::vector<std::byte>& target, std::size_t offset, std::span<const std::byte> raw) {
	std::size_t at = offset;
	for (const std::byte value : raw) {
		target.at(at) = value;
		++at;
	}
}

} // namespace revenant::imagegen
