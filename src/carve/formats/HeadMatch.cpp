// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeadMatch.hpp"

#include <algorithm>
#include <cstddef>
#include <span>

#include "revenant/core/ByteReader.hpp"

namespace revenant::carve {

bool headMatches(const ByteReader& reader, std::span<const std::byte> signature) {
	const auto head = reader.bytes(0, signature.size());
	return head.hasValue() && std::ranges::equal(head.value(), signature);
}

} // namespace revenant::carve
