// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CarverRegistry.hpp"

namespace revenant::testing {

// Every carver that ships, registered — what a run with no `--format` filter
// searches for. Three integration harnesses needed exactly this and each wrote
// it out; a fourth would have been the point at which nobody noticed one of
// them drifting.
[[nodiscard]] inline carve::CarverRegistry builtinRegistry() {
	carve::CarverRegistry registry;
	carve::registerBuiltinCarvers(registry);
	return registry;
}

} // namespace revenant::testing
