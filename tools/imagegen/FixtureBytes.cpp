// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/FixtureBytes.hpp"

#include <cstddef>
#include <vector>

namespace revenant::imagegen {

std::vector<std::byte> fixtureContent(std::size_t sizeBytes, std::byte seed) {
	const auto offset = std::to_integer<std::size_t>(seed);
	std::vector<std::byte> content;
	content.reserve(sizeBytes);
	for (std::size_t at = 0; at < sizeBytes; ++at) {
		content.push_back(static_cast<std::byte>((at + offset) % 251U));
	}
	return content;
}

} // namespace revenant::imagegen
