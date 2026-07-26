// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "revenant/core/Confidence.hpp"

namespace revenant::carve {

// The outcome of one validation attempt: an exact extent and a verdict.
struct CarveResult {
	std::uint64_t length;  // exact extent in bytes (0 when kRejected)
	Confidence confidence; // validation verdict
	std::string extension; // e.g. "jpg"
};

} // namespace revenant::carve
