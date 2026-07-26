// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

// Validating JPEG carver (ADR-0003): walks the marker structure
// SOI -> segments -> SOS + entropy-coded data -> EOI and returns the EXACT
// extent. Never collects bytes to a footer guess or a size cap.
class JpegCarver final : public FormatCarver {
public:
	[[nodiscard]] std::span<const Signature> signatures() const override;
	[[nodiscard]] Result<CarveResult> carve(ByteReader& reader) const override;
};

} // namespace revenant::carve
