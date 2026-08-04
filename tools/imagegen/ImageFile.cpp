// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ImageFile.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <ostream>
#include <span>

#include "revenant/core/Result.hpp"

namespace revenant::imagegen {

void writeBytesTo(std::ostream& stream, std::span<const std::byte> bytes) {
	// Streamed through an output iterator rather than a cast buffer: no
	// pointer laundering, and a fixture is not a throughput concern.
	std::ranges::transform(bytes, std::ostreambuf_iterator<char>{stream}, [](std::byte value) {
		return static_cast<char>(value);
	});
}

Result<std::uint64_t>
writeImageBytes(const std::filesystem::path& path, std::span<const std::byte> image) {
	return writeImageFile(path, [image](std::ostream& stream) {
		writeBytesTo(stream, image);
		return static_cast<std::uint64_t>(image.size());
	});
}

} // namespace revenant::imagegen
