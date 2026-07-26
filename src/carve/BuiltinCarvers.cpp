// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/carve/BuiltinCarvers.hpp"

#include <memory>

#include "formats/JpegCarver.hpp"
#include "revenant/carve/CarverRegistry.hpp"

namespace revenant::carve {

void registerBuiltinCarvers(CarverRegistry& registry) {
	registry.registerCarver(std::make_unique<JpegCarver>());
}

} // namespace revenant::carve
