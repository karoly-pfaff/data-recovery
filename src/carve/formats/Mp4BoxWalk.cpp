// SPDX-License-Identifier: GPL-3.0-or-later
#include "Mp4BoxWalk.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

namespace {

constexpr std::size_t kTypeBytes = 4;
constexpr std::uint64_t kShortHeaderBytes = 8;
constexpr std::uint64_t kLongHeaderBytes = 16;
constexpr std::uint32_t kLargeSizeMarker = 1;
constexpr std::byte kFirstPrintable{0x20};
constexpr std::byte kLastPrintable{0x7E};

constexpr std::array<std::byte, kTypeBytes> kFtyp{
	std::byte{'f'},
	std::byte{'t'},
	std::byte{'y'},
	std::byte{'p'}};
constexpr std::array<std::byte, kTypeBytes> kMoov{
	std::byte{'m'},
	std::byte{'o'},
	std::byte{'o'},
	std::byte{'v'}};
constexpr std::array<std::byte, kTypeBytes> kMdat{
	std::byte{'m'},
	std::byte{'d'},
	std::byte{'a'},
	std::byte{'t'}};

struct Box {
	std::span<const std::byte> type;
	std::uint64_t next = 0;
};

// A real box type is four printable ASCII characters. This is what stops the
// walk from absorbing trailing garbage that happens to follow a file.
[[nodiscard]] bool typeIsPrintable(std::span<const std::byte> type) {
	return std::ranges::all_of(type, [](std::byte value) {
		return value >= kFirstPrintable && value <= kLastPrintable;
	});
}

// The 64-bit form, which must still cover its own 16-byte header.
[[nodiscard]] Result<std::uint64_t> readLargeSize(const ByteReader& reader, std::uint64_t offset) {
	return reader.readBe<std::uint64_t>(offset + kShortHeaderBytes)
		.andThen([offset](std::uint64_t size) {
			if (size < kLongHeaderBytes) {
				return Result<std::uint64_t>(
					Error{.code = ErrorCode::kInvalidArgument, .offset = offset});
			}
			return Result<std::uint64_t>(size);
		});
}

// `declared` comes first so the ByteReader separates it from `offset`: two
// adjacent, mutually convertible integers here would be a swap waiting to
// happen. A declared size of 0 means "to end of file", which for a candidate
// embedded in a byte stream is precisely unknowable — so it is refused.
[[nodiscard]] Result<std::uint64_t>
sizeFromDeclared(std::uint32_t declared, const ByteReader& reader, std::uint64_t offset) {
	if (declared == kLargeSizeMarker) {
		return readLargeSize(reader, offset);
	}
	if (declared < kShortHeaderBytes) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = offset};
	}
	return static_cast<std::uint64_t>(declared);
}

[[nodiscard]] Result<std::uint64_t> readBoxSize(const ByteReader& reader, std::uint64_t offset) {
	return reader.readBe<std::uint32_t>(offset).andThen([&reader, offset](std::uint32_t declared) {
		return sizeFromDeclared(declared, reader, offset);
	});
}

[[nodiscard]] Result<Box>
makeBox(std::uint64_t offset, std::span<const std::byte> type, std::uint64_t size) {
	if (!typeIsPrintable(type)) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = offset};
	}
	return Box{.type = type, .next = offset + size};
}

// Reading the whole box is what proves it fits; the type comes from that same
// span, so no bound is taken on trust.
[[nodiscard]] Result<Box> readBox(const ByteReader& reader, std::uint64_t offset) {
	return readBoxSize(reader, offset).andThen([&reader, offset](std::uint64_t size) {
		if (size > reader.size()) {
			return Result<Box>(Error{.code = ErrorCode::kOutOfRange, .offset = offset});
		}
		return reader.bytes(offset, static_cast<std::size_t>(size))
			.andThen([offset, size](std::span<const std::byte> whole) {
				return makeBox(offset, whole.subspan(kTypeBytes, kTypeBytes), size);
			});
	});
}

[[nodiscard]] bool typeIs(const Box& box, std::span<const std::byte> name) {
	return std::ranges::equal(box.type, name);
}

// Folds one box into the outcome. The first box must be `ftyp`.
[[nodiscard]] bool acceptBox(Mp4WalkOutcome& outcome, const Box& box) {
	if (!outcome.sawFtyp && !typeIs(box, kFtyp)) {
		return false;
	}
	outcome.sawFtyp = true;
	outcome.sawMoov = outcome.sawMoov || typeIs(box, kMoov);
	outcome.sawMdat = outcome.sawMdat || typeIs(box, kMdat);
	outcome.end = box.next;
	return true;
}

enum class WalkStep : std::uint8_t { kContinue, kStop, kNotMp4 };

[[nodiscard]] WalkStep
stepBox(const ByteReader& reader, Mp4WalkOutcome& outcome, std::uint64_t& offset) {
	const auto box = readBox(reader, offset);
	if (!box.hasValue()) {
		return WalkStep::kStop;
	}
	if (!acceptBox(outcome, box.value())) {
		return WalkStep::kNotMp4;
	}
	offset = box.value().next;
	return WalkStep::kContinue;
}

} // namespace

Mp4WalkOutcome walkMp4Boxes(const ByteReader& reader) {
	Mp4WalkOutcome outcome;
	std::uint64_t offset = 0;
	auto step = WalkStep::kContinue;
	// Every accepted box advances `offset` by at least its 8-byte header, so
	// the walk is bounded by the buffer without a separate iteration cap.
	while (step == WalkStep::kContinue) {
		step = stepBox(reader, outcome, offset);
	}
	return step == WalkStep::kNotMp4 ? Mp4WalkOutcome{} : outcome;
}

} // namespace revenant::carve
