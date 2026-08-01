// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/PathSafeByte.hpp"

#include <cstddef>

namespace revenant::fs {

bool passesThroughAsItself(std::byte raw) noexcept {
	const auto value = std::to_integer<unsigned>(raw);
	return value >= 0x20U && value <= 0x7EU && raw != std::byte{'/'} && raw != std::byte{'%'};
}

} // namespace revenant::fs
