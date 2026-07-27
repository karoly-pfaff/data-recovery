// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

// Validating PNG carver (ADR-0003): walks the chunk list
// signature -> IHDR -> ... -> IEND, verifying every chunk's CRC-32, and
// returns the EXACT extent. A chunk whose CRC fails ends the trusted prefix
// rather than being waved through.
class PngCarver final : public FormatCarver {
public:
	[[nodiscard]] std::span<const Signature> signatures() const override;
	[[nodiscard]] Result<CarveResult> carve(ByteReader& reader) const override;
};

} // namespace revenant::carve
