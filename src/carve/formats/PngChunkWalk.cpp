// SPDX-License-Identifier: GPL-3.0-or-later
#include "PngChunkWalk.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Crc32.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

namespace {

constexpr std::uint64_t kSignatureBytes = 8;
constexpr std::size_t kTypeBytes = 4;
constexpr std::size_t kLengthBytes = 4;
constexpr std::size_t kCrcBytes = 4;
constexpr std::size_t kChunkOverhead = kLengthBytes + kTypeBytes + kCrcBytes;
// The spec caps a chunk's data at 2^31-1; anything above it is corruption,
// and bounding it here keeps a hostile length out of the arithmetic below.
constexpr std::uint32_t kMaxChunkData = 0x7FFFFFFFU;

constexpr std::array<std::byte, kTypeBytes> kIhdr{
	std::byte{'I'},
	std::byte{'H'},
	std::byte{'D'},
	std::byte{'R'}};
constexpr std::array<std::byte, kTypeBytes> kIend{
	std::byte{'I'},
	std::byte{'E'},
	std::byte{'N'},
	std::byte{'D'}};

// One chunk as located in the buffer: its type, the bytes the CRC covers
// (type and data are contiguous on disk), and where the next chunk begins.
struct Chunk {
	std::span<const std::byte> type;
	std::span<const std::byte> covered;
	std::uint32_t storedCrc = 0;
	std::uint64_t next = 0;
};

// The declared data length, refused before it takes part in any arithmetic if
// it exceeds what the spec allows.
[[nodiscard]] Result<std::uint32_t>
readChunkLength(const ByteReader& reader, std::uint64_t offset) {
	return reader.readBe<std::uint32_t>(offset).andThen([offset](std::uint32_t length) {
		if (length > kMaxChunkData) {
			return Result<std::uint32_t>(Error{.code = ErrorCode::kOutOfRange, .offset = offset});
		}
		return Result<std::uint32_t>(length);
	});
}

[[nodiscard]] Chunk
makeChunk(std::uint64_t offset, std::span<const std::byte> covered, std::uint32_t storedCrc) {
	return Chunk{
		.type = covered.first(kTypeBytes),
		.covered = covered,
		.storedCrc = storedCrc,
		.next = offset + kChunkOverhead + (covered.size() - kTypeBytes)};
}

// Reads the chunk at `offset`, or nothing if it does not fit in the buffer.
// Every bound the on-disk length participates in is checked before use.
[[nodiscard]] Result<Chunk> readChunk(const ByteReader& reader, std::uint64_t offset) {
	return readChunkLength(reader, offset).andThen([&reader, offset](std::uint32_t length) {
		return reader.bytes(offset + kLengthBytes, kTypeBytes + length)
			.andThen([&reader, offset](std::span<const std::byte> covered) {
				return reader.readBe<std::uint32_t>(offset + kLengthBytes + covered.size())
					.map([offset, covered](std::uint32_t storedCrc) {
						return makeChunk(offset, covered, storedCrc);
					});
			});
	});
}

[[nodiscard]] bool typeIs(const Chunk& chunk, std::span<const std::byte> name) {
	return std::ranges::equal(chunk.type, name);
}

[[nodiscard]] bool crcVerifies(const Chunk& chunk) {
	return crc32(chunk.covered) == chunk.storedCrc;
}

// Folds one verified chunk into the outcome. The first chunk must be IHDR;
// anything else means these bytes are not a PNG, so the walk yields nothing.
[[nodiscard]] bool acceptChunk(PngWalkOutcome& outcome, const Chunk& chunk) {
	if (!outcome.sawIhdr && !typeIs(chunk, kIhdr)) {
		return false;
	}
	outcome.sawIhdr = true;
	outcome.end = chunk.next;
	outcome.reachedIend = typeIs(chunk, kIend);
	return true;
}

enum class WalkStep : std::uint8_t { kContinue, kStop, kNotPng };

[[nodiscard]] WalkStep
stepChunk(const ByteReader& reader, PngWalkOutcome& outcome, std::uint64_t& offset) {
	const auto chunk = readChunk(reader, offset);
	if (!chunk.hasValue() || !crcVerifies(chunk.value())) {
		return WalkStep::kStop;
	}
	if (!acceptChunk(outcome, chunk.value())) {
		return WalkStep::kNotPng;
	}
	offset = chunk.value().next;
	return WalkStep::kContinue;
}

} // namespace

PngWalkOutcome walkPngChunks(const ByteReader& reader) {
	PngWalkOutcome outcome{.end = kSignatureBytes, .sawIhdr = false, .reachedIend = false};
	std::uint64_t offset = kSignatureBytes;
	auto step = WalkStep::kContinue;
	// Each chunk advances `offset` by at least kChunkOverhead, so the walk is
	// bounded by the buffer size without a separate iteration cap.
	while (step == WalkStep::kContinue && !outcome.reachedIend) {
		step = stepChunk(reader, outcome, offset);
	}
	return step == WalkStep::kNotPng ? PngWalkOutcome{} : outcome;
}

} // namespace revenant::carve
