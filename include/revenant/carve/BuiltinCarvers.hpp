// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string_view>

#include "revenant/carve/CarverRegistry.hpp"

namespace revenant::carve {

// Registers every built-in FormatCarver. New formats extend this single
// registration point (revenant:add-format-carver skill).
void registerBuiltinCarvers(CarverRegistry& registry);

// The same, restricted to the named extensions. Filtering at registration
// rather than at report time is the point: an excluded format then costs
// nothing at all — no signature search, no carve attempt. An empty allowlist
// registers everything, so "no filter" is the default rather than "nothing
// works".
void registerBuiltinCarvers(CarverRegistry& registry, std::span<const std::string_view> allowlist);

} // namespace revenant::carve
