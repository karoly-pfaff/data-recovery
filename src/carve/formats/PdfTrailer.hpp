// SPDX-License-Identifier: GPL-3.0-or-later
// Internal. Locating and checking a PDF's trailing `startxref` / `%%EOF` pair,
// which is what fixes the file's end. Not a public interface.
#pragma once

#include <cstdint>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

// The file's last trailer.
struct PdfTrailer {
	std::uint64_t end = 0;               // just past `%%EOF` and its line ending
	bool crossReferenceResolves = false; // startxref pointed somewhere usable
};

// Finds the last `%%EOF` and checks the `startxref` behind it. kNotFound when
// the data holds no end marker: a PDF whose end cannot be located has no
// extent this carver is willing to claim.
[[nodiscard]] Result<PdfTrailer> findPdfTrailer(const ByteReader& reader);

} // namespace revenant::carve
