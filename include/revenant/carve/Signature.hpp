// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

namespace revenant::carve {

// A magic-byte pattern that triggers a validation attempt (a hypothesis,
// not a file — ADR-0003).
struct Signature {
	std::span<const std::byte> magic; // bytes to match
	std::size_t offset = 0;           // where in the file the magic sits (usually 0)
};

} // namespace revenant::carve
