// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/carve/BuiltinCarvers.hpp"

#include <memory>

#include "formats/JpegCarver.hpp"
#include "formats/Mp4Carver.hpp"
#include "formats/PngCarver.hpp"
#include "formats/RawCarver.hpp"
#include "revenant/carve/CarverRegistry.hpp"

namespace revenant::carve {

void registerBuiltinCarvers(CarverRegistry& registry) {
	registry.registerCarver(std::make_unique<JpegCarver>());
	registry.registerCarver(std::make_unique<PngCarver>());
	registry.registerCarver(std::make_unique<Mp4Carver>());
	registry.registerCarver(std::make_unique<RawCarver>());
}

} // namespace revenant::carve
