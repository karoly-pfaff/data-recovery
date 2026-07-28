// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "revenant/carve/CarveResult.hpp"

namespace revenant::carve {

// The smallest size a real file of this kind can plausibly have. Structure
// alone cannot separate a file from a coincidence: a 22-byte sequence can be a
// structurally perfect JPEG, and enough random data will produce some. This is
// a floor, not a content heuristic — everything above it is still decided by
// the format's own structure (ADR-0003).
[[nodiscard]] std::uint64_t plausibleMinimumBytes(std::string_view extension);

// Downgrades a result whose extent falls below its format's floor to
// kRejected. Nothing is ever upgraded, and nothing is dropped: what to do with
// a weak candidate is arbitration's decision (ADR-0006), not this filter's.
[[nodiscard]] CarveResult applyPlausibility(CarveResult result);

} // namespace revenant::carve
