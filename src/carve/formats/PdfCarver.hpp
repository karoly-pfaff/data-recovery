// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

// Validating PDF carver (ADR-0003): the file ends at its *last* `%%EOF`, not
// its first — an incrementally saved PDF carries one per revision. The
// `startxref` offset behind that marker is resolved, so a `%%EOF` that is just
// a string in the data does not get vouched for.
class PdfCarver final : public FormatCarver {
public:
	[[nodiscard]] std::span<const Signature> signatures() const override;
	[[nodiscard]] Result<CarveResult> carve(ByteReader& reader) const override;
};

} // namespace revenant::carve
