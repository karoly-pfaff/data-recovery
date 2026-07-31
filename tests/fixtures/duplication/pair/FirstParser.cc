// SPDX-License-Identifier: GPL-3.0-or-later
// Duplication-gate fixture: `readLittleEndian` below is byte-for-byte the same
// knowledge as the copy in SecondParser.cc — the case the gate exists to fail.

#include <cstdint>

namespace fixture::first {

std::uint32_t readLittleEndian(const unsigned char* bytes, unsigned int offset) {
	std::uint32_t value = 0;
	value |= static_cast<std::uint32_t>(bytes[offset]);
	value |= static_cast<std::uint32_t>(bytes[offset + 1]) << 8U;
	value |= static_cast<std::uint32_t>(bytes[offset + 2]) << 16U;
	value |= static_cast<std::uint32_t>(bytes[offset + 3]) << 24U;
	return value;
}

bool looksLikeHeader(const unsigned char* bytes) {
	return readLittleEndian(bytes, 0) == 0x1234ABCDU;
}

} // namespace fixture::first
