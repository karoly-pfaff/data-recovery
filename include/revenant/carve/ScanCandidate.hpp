// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "revenant/carve/CarveResult.hpp"

namespace revenant::carve {

// One verdict-carrying discovery: where a signature matched and what the
// carver concluded. Consumed by the candidate index (ADR-0006) later.
struct ScanCandidate {
	std::uint64_t offset = 0; // device offset of the candidate's first byte
	CarveResult result;
};

} // namespace revenant::carve
