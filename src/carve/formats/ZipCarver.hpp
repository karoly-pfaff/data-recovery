// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

// Validating ZIP carver (ADR-0003): the extent comes from the End Of Central
// Directory record, checked against the central directory it describes rather
// than merely found. Office documents are ZIP archives, so the entry names
// decide whether the file is reported as docx, xlsx, pptx, or plain zip.
class ZipCarver final : public FormatCarver {
public:
	[[nodiscard]] std::span<const Signature> signatures() const override;
	[[nodiscard]] Result<CarveResult> carve(ByteReader& reader) const override;
};

} // namespace revenant::carve
