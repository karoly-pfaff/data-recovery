// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace revenant {

// Cross-layer verdict currency: carve validation results, filesystem
// recoverability grades, and candidate arbitration all speak this scale.
// Ordering matters: higher is more trustworthy (arbitration compares).
enum class Confidence : std::uint8_t { kRejected, kUncertain, kValid };

} // namespace revenant
