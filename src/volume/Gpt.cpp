// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/volume/Gpt.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "volume/GptInternal.hpp"

namespace revenant::volume {

namespace {

// The header proper, once the sector has been narrowed to the bytes the header
// says it occupies — which is also the range its checksum covers.
[[nodiscard]] Result<GptHeader>
verifiedHeader(std::span<const std::byte> header, std::uint64_t atLba) {
	const ByteReader reader{header};
	return checksumIsValid(header)
		.andThen([&](bool) { return placedAt(reader, atLba); })
		.andThen([&](bool) { return headerBodyOf(reader); });
}

} // namespace

Result<GptHeader> parseGptHeader(std::span<const std::byte> sector, std::uint64_t atLba) {
	if (sector.size() < kGptHeaderBytes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = sector.size()};
	}
	const ByteReader reader{sector};
	return namesGpt(reader)
		.andThen([&](bool) { return headerSizeIn(reader, sector.size()); })
		.andThen([&](std::size_t size) { return verifiedHeader(sector.first(size), atLba); });
}

} // namespace revenant::volume
