// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

namespace revenant::imagegen {

// A structurally valid JPEG (SOI → EOI) of exactly `sizeBytes`, deterministic
// and free of raw 0xFF bytes in the entropy run, so the carver validates it
// rather than merely finding a header. Nothing about it is a filesystem's
// business: an NTFS volume's file data and a bare carve corpus want the same
// thing, which is a JPEG that is really a JPEG.
[[nodiscard]] std::vector<std::byte> fixtureJpeg(std::size_t sizeBytes);

} // namespace revenant::imagegen
