// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

// Validating camera-RAW carver (ADR-0003). RAW files are TIFF containers, so
// the extent comes from walking the IFD chain and taking the highest offset
// anything in the file points at — normally the end of the image data itself.
// The camera that wrote the file decides the extension: cr2, nef, arw, or tif.
class RawCarver final : public FormatCarver {
public:
	[[nodiscard]] std::span<const Signature> signatures() const override;
	[[nodiscard]] Result<CarveResult> carve(ByteReader& reader) const override;
};

} // namespace revenant::carve
