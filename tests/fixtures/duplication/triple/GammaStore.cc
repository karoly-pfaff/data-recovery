// SPDX-License-Identifier: GPL-3.0-or-later
// Duplication-gate fixture: the third home of AlphaStore.cc's encoder.

#include <cstdint>
#include <vector>

namespace fixture::gamma {

std::vector<std::uint8_t> encode(const std::vector<std::uint32_t>& words) {
	std::vector<std::uint8_t> out;
	out.reserve(words.size() * 4);
	for (const auto word : words) {
		out.push_back(static_cast<std::uint8_t>(word));
		out.push_back(static_cast<std::uint8_t>(word >> 8U));
		out.push_back(static_cast<std::uint8_t>(word >> 16U));
		out.push_back(static_cast<std::uint8_t>(word >> 24U));
	}
	return out;
}

} // namespace fixture::gamma
