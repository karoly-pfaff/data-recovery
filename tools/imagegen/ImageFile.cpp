// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ImageFile.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::imagegen {

Result<std::uint64_t>
writeImageBytes(const std::filesystem::path& path, std::span<const std::byte> image) {
	std::ofstream stream{path, std::ios::binary | std::ios::trunc};
	// Streamed through an output iterator rather than a cast buffer: no
	// pointer laundering, and a fixture is not a throughput concern.
	std::ranges::transform(image, std::ostreambuf_iterator<char>{stream}, [](std::byte value) {
		return static_cast<char>(value);
	});
	if (!stream.good()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return static_cast<std::uint64_t>(image.size());
}

} // namespace revenant::imagegen
