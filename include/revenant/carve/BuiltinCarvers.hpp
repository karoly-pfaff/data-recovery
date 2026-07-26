// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "revenant/carve/CarverRegistry.hpp"

namespace revenant::carve {

// Registers every built-in FormatCarver (M1: JPEG). Later formats extend
// this single registration point (revenant:add-format-carver skill).
void registerBuiltinCarvers(CarverRegistry& registry);

} // namespace revenant::carve
