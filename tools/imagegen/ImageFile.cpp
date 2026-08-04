// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ImageFile.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <span>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::imagegen {

namespace {

// One byte into the stream; false once it has stopped taking them. Through an
// output iterator rather than a cast buffer: no pointer laundering, and a
// fixture is not a throughput concern.
[[nodiscard]] bool put(std::ostreambuf_iterator<char>& out, std::byte value) {
	*out++ = static_cast<char>(value);
	return !out.failed();
}

} // namespace

Result<std::uint64_t> closeImage(std::ofstream& stream, FilledImage filled) {
	stream.close();
	if (!filled.complete) {
		return Error{.code = ErrorCode::kIoFailure, .offset = filled.written};
	}
	if (!stream.good()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return filled.written;
}

std::uint64_t writeBytesTo(std::ostream& stream, std::span<const std::byte> bytes) {
	// Counted a byte at a time because the iterator does not stop when the
	// stream goes bad, and a count that kept going would become the caller's
	// error offset.
	std::ostreambuf_iterator<char> out{stream};
	std::uint64_t written = 0;
	for (const std::byte value : bytes) {
		if (!put(out, value)) {
			break;
		}
		++written;
	}
	return written;
}

Result<std::uint64_t>
writeImageBytes(const std::filesystem::path& path, std::span<const std::byte> image) {
	return writeImageFile(path, [image](std::ostream& stream) {
		return writeBytesTo(stream, image);
	});
}

} // namespace revenant::imagegen
