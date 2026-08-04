// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iosfwd>
#include <span>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::imagegen {

// Writes `bytes` into an already open stream and returns how many of them the
// stream took — short of `bytes.size()` only when it failed. The half of
// `writeImageBytes` below without the file, for a builder that puts content
// into a stream it is already filling from somewhere else.
//
// The count is returned rather than assumed because an output iterator keeps
// going after the stream goes bad, so a caller that reported `bytes.size()`
// would name an offset past where writing stopped.
[[nodiscard]] std::uint64_t writeBytesTo(std::ostream& stream, std::span<const std::byte> bytes);

// Opens `path` as a fresh image, hands the stream to `fill`, closes it, and
// reports the first of those that failed.
//
// How this tool opens an image and how it reports a failed write are one fact,
// and three builders — the in-memory one below, the pattern writer and the soak
// writer — each spelled it out. Now they call this.
//
// **The close is the point.** Judging `stream.good()` before it asks whether the
// bytes reached a buffer, not whether they reached a disk: anything smaller than
// the filebuf's buffer has not been written at all yet, so a full disk would
// come back as a complete image. A 256-plant plan file is five kilobytes — the
// soak's entire ground truth, inside one buffer — and this is what stops it
// being reported as written when it is not.
// What a finished image write amounts to: `written` when the fill and the close
// both succeeded, an error at `written` when the fill stopped there, and an
// error with no offset when the close refused what the buffer had taken — how
// much of a failed flush reached the file is not knowable, and `Error::offset`
// is meaningful or absent. Separate from the template below because the two
// answer different questions, and because only one of them has to be a template.
[[nodiscard]] Result<std::uint64_t>
imageOutcome(const std::ofstream& closed, bool filled, std::uint64_t written);

template <typename Fill>
[[nodiscard]] Result<std::uint64_t> writeImageFile(const std::filesystem::path& path, Fill fill) {
	std::ofstream stream{path, std::ios::binary | std::ios::trunc};
	const std::uint64_t written = fill(stream);
	const bool filled = stream.good();
	stream.close();
	return imageOutcome(stream, filled, written);
}

// Writes a whole image that was built in memory to `path`; returns the bytes
// written. Every builder that assembles its volume as one buffer ends here.
[[nodiscard]] Result<std::uint64_t>
writeImageBytes(const std::filesystem::path& path, std::span<const std::byte> image);

} // namespace revenant::imagegen
