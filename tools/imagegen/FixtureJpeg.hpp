// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace revenant::imagegen {

// A structurally valid JPEG (SOI → EOI) of exactly `sizeBytes`, deterministic
// and free of raw 0xFF bytes in the entropy run, so the carver validates it
// rather than merely finding a header. Nothing about it is a filesystem's
// business: an NTFS volume's file data and a bare carve corpus want the same
// thing, which is a JPEG that is really a JPEG.
[[nodiscard]] std::vector<std::byte> fixtureJpeg(std::size_t sizeBytes);

// Writes `token` into the entropy run, so two JPEGs of the same size are two
// different files. It lives here rather than at the call site because what may
// be written there — anything except a raw 0xFF, which would read as a marker —
// is this fixture's own knowledge. A JPEG shorter than the frame is left alone.
void stampJpegPayload(std::span<std::byte> jpeg, std::uint64_t token);

} // namespace revenant::imagegen
