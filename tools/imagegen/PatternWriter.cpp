// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/PatternWriter.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string_view>

#include "revenant/core/Endian.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::imagegen {

namespace {

void fillCounter(std::span<std::byte> sector, std::uint64_t lba) {
	for (std::size_t j = 0; j < sector.size(); ++j) {
		// Bounds are guaranteed by the loop condition (j < sector.size());
		// std::span has no checked accessor (operator[] only) in C++20.
		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		sector[j] = static_cast<std::byte>(((lba * kSectorBytes) + j) & 0xFFU);
	}
}

void fillLbaTag(std::span<std::byte> sector, std::uint64_t lba) {
	std::ranges::fill(sector, std::byte{0});
	const auto tag = toLittleEndian<std::uint64_t>(lba);
	std::copy_n(tag.begin(), std::min(tag.size(), sector.size()), sector.begin());
}

// One sector's bytes as chars for ofstream (bit_cast: no reinterpret_cast).
std::array<char, kSectorBytes> asChars(const std::array<std::byte, kSectorBytes>& sector) {
	return std::bit_cast<std::array<char, kSectorBytes>>(sector);
}

} // namespace

Result<Pattern> parsePattern(std::string_view name) noexcept {
	if (name == "zero") {
		return Pattern::kZero;
	}
	if (name == "counter") {
		return Pattern::kCounter;
	}
	if (name == "lba") {
		return Pattern::kLbaTag;
	}
	return Error{.code = ErrorCode::kInvalidArgument};
}

void fillSector(std::span<std::byte> sector, std::uint64_t lba, Pattern pattern) noexcept {
	switch (pattern) {
	case Pattern::kZero:
		std::ranges::fill(sector, std::byte{0});
		return;
	case Pattern::kCounter:
		fillCounter(sector, lba);
		return;
	case Pattern::kLbaTag:
		fillLbaTag(sector, lba);
		return;
	}
}

namespace {

// Writes one sector-sized (or, at `to`, shorter) chunk at device offset `at`;
// returns the offset just past it. Split out of writeFiller() to keep that
// function under the 10-statement limit.
std::uint64_t
writeChunk(std::ostream& stream, Pattern pattern, std::uint64_t at, std::uint64_t to) {
	std::array<std::byte, kSectorBytes> sector{};
	fillSector(sector, at / kSectorBytes, pattern);
	const auto chunk = std::min<std::uint64_t>(kSectorBytes, to - at);
	stream.write(asChars(sector).data(), static_cast<std::streamsize>(chunk));
	return at + chunk;
}

} // namespace

std::uint64_t
writeFiller(std::ostream& stream, std::uint64_t from, std::uint64_t to, Pattern pattern) {
	std::uint64_t at = from;
	while (at < to && stream.good()) {
		at = writeChunk(stream, pattern, at, to);
	}
	return at;
}

Result<std::uint64_t>
writeImage(const std::filesystem::path& outputPath, std::uint64_t sizeBytes, Pattern pattern) {
	std::ofstream stream{outputPath, std::ios::binary | std::ios::trunc};
	const auto written = writeFiller(stream, 0, sizeBytes, pattern);
	if (!stream.good()) {
		return Error{.code = ErrorCode::kIoFailure, .offset = written};
	}
	return written;
}

} // namespace revenant::imagegen
