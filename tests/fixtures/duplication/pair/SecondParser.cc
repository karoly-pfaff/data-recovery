// SPDX-License-Identifier: GPL-3.0-or-later
// Duplication-gate fixture: the second home of FirstParser.cc's decoder.

#include <cstdint>

namespace fixture::second {

std::uint32_t readLittleEndian(const unsigned char* bytes, unsigned int offset) {
	std::uint32_t value = 0;
	value |= static_cast<std::uint32_t>(bytes[offset]);
	value |= static_cast<std::uint32_t>(bytes[offset + 1]) << 8U;
	value |= static_cast<std::uint32_t>(bytes[offset + 2]) << 16U;
	value |= static_cast<std::uint32_t>(bytes[offset + 3]) << 24U;
	return value;
}

unsigned int sectorCount(const unsigned char* bytes) {
	return readLittleEndian(bytes, 8) / 512U;
}

} // namespace fixture::second
