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

// Opens `path` as a fresh image, hands the stream to `fill`, and turns a stream
// that went bad into a typed error at the offset `fill` says it reached.
//
// How this tool opens an image and how it reports a failed write are one fact,
// and three builders — the in-memory one below, the pattern writer and the soak
// writer — each spelled it out. Now they call this.
template <typename Fill>
[[nodiscard]] Result<std::uint64_t> writeImageFile(const std::filesystem::path& path, Fill fill) {
	std::ofstream stream{path, std::ios::binary | std::ios::trunc};
	const std::uint64_t written = fill(stream);
	if (!stream.good()) {
		return Error{.code = ErrorCode::kIoFailure, .offset = written};
	}
	return written;
}

// Writes a whole image that was built in memory to `path`; returns the bytes
// written. Every builder that assembles its volume as one buffer ends here.
[[nodiscard]] Result<std::uint64_t>
writeImageBytes(const std::filesystem::path& path, std::span<const std::byte> image);

} // namespace revenant::imagegen
