// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string_view>

#include "revenant/carve/CarverRegistry.hpp"

namespace revenant::carve {

// Registers every built-in FormatCarver. New formats extend this single
// registration point (add-format-carver skill).
void registerBuiltinCarvers(CarverRegistry& registry);

// The same, restricted to the named extensions. Filtering at registration
// rather than at report time is the point: an excluded format then costs
// nothing at all — no signature search, no carve attempt. An empty allowlist
// registers everything, so "no filter" is the default rather than "nothing
// works".
void registerBuiltinCarvers(CarverRegistry& registry, std::span<const std::string_view> allowlist);

// Every extension an allowlist may name, in registration order — the same
// lists the filter above matches against, flattened, so what a frontend offers
// and what actually registers cannot drift apart.
[[nodiscard]] std::span<const std::string_view> builtinFormatNames();

// Whether any built-in carver reports `name` as its extension, which is what
// makes an allowlist entry meaningful rather than a silent narrowing to
// nothing.
[[nodiscard]] bool isBuiltinFormat(std::string_view name);

} // namespace revenant::carve
