// SPDX-License-Identifier: GPL-3.0-or-later
#include "JpegMarkerWalk.hpp"

#include <cstdint>
#include <optional>

#include "JpegEntropyScan.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

namespace {

constexpr std::uint8_t kMarkerPrefix = 0xFF;
constexpr std::uint8_t kMarkerEoi = 0xD9;
constexpr std::uint8_t kMarkerSos = 0xDA;
constexpr std::uint8_t kMarkerTem = 0x01;
constexpr std::uint8_t kMarkerRstFirst = 0xD0;
constexpr std::uint8_t kMarkerRstLast = 0xD7;
constexpr std::uint16_t kMinSegmentLength = 2;
constexpr std::uint64_t kSoiBytes = 2;
constexpr std::uint64_t kMarkerPairBytes = 2;

// Walk bookkeeping: where we are, and whether entropy data has started
// (the boundary between Rejected and Uncertain on a structural break).
struct Walk {
	std::uint64_t pos = kSoiBytes;
	bool sawSos = false;
};

bool isRestartMarker(std::uint8_t code) {
	return code >= kMarkerRstFirst && code <= kMarkerRstLast;
}

bool isStandalone(std::uint8_t code) {
	return code == kMarkerTem || isRestartMarker(code);
}

// Repeated 0xFF bytes before a marker code are legal fill; returns the
// position of the LAST prefix byte.
std::uint64_t skipFillBytes(ByteReader& reader, std::uint64_t pos) {
	auto next = reader.readLe<std::uint8_t>(pos + 1);
	while (next.hasValue() && next.value() == kMarkerPrefix) {
		++pos;
		next = reader.readLe<std::uint8_t>(pos + 1);
	}
	return pos;
}

// Validates the marker prefix at `pos` (must be 0xFF) and returns the
// position of the LAST fill byte before the marker code, skipping any
// legal 0xFF fill in between.
Result<std::uint64_t> readMarkerPrefix(ByteReader& reader, std::uint64_t pos) {
	const auto prefix = reader.readLe<std::uint8_t>(pos);
	if (!prefix.hasValue()) {
		return prefix.error();
	}
	if (prefix.value() != kMarkerPrefix) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = pos};
	}
	return skipFillBytes(reader, pos);
}

// Reads the 1-byte marker code just past an already-validated prefix and
// advances `pos` past the marker pair.
Result<std::uint8_t>
readCodeAfterPrefix(ByteReader& reader, std::uint64_t prefixEnd, std::uint64_t& pos) {
	const auto code = reader.readLe<std::uint8_t>(prefixEnd + 1);
	if (!code.hasValue()) {
		return code;
	}
	pos = prefixEnd + kMarkerPairBytes;
	return code;
}

// Reads the marker code at `pos` (which must hold 0xFF, possibly after
// legal fill) and advances past the marker pair. Out-of-range and a non-FF
// prefix are typed errors the walker folds into its verdict.
Result<std::uint8_t> readMarkerCode(ByteReader& reader, std::uint64_t& pos) {
	const auto prefixEnd = readMarkerPrefix(reader, pos);
	if (!prefixEnd.hasValue()) {
		return prefixEnd.error();
	}
	return readCodeAfterPrefix(reader, prefixEnd.value(), pos);
}

// Rejects a declared segment length that would run past the available
// bytes — an over-declared length on truncated/hostile data is exactly the
// "grab bytes past validated structure" failure this carver must reject.
Result<std::uint64_t>
validatedSegmentEnd(const ByteReader& reader, std::uint64_t pos, std::uint16_t length) {
	if (length < kMinSegmentLength) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = pos};
	}
	const auto end = pos + length;
	if (end > reader.size()) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = pos};
	}
	return end;
}

// A marker segment's big-endian length includes its own two bytes.
Result<std::uint64_t> skipSegment(ByteReader& reader, std::uint64_t pos) {
	const auto length = reader.readBe<std::uint16_t>(pos);
	if (!length.hasValue()) {
		return length.error();
	}
	return validatedSegmentEnd(reader, pos, length.value());
}

// Folds an entropy-scan outcome into the walk: pos always advances to
// where the scan ended; exhaustion (no terminator) ends the walk without
// EOI, the same "broken" signal every other structural break already uses.
Result<bool> continueAfterEntropy(Walk& walk, const EntropyOutcome& outcome) {
	walk.pos = outcome.pos;
	if (!outcome.foundTerminator) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = outcome.pos};
	}
	return false;
}

// After an SOS segment header, entropy data runs to the next real marker.
Result<bool> applyEntropyIfSos(ByteReader& reader, Walk& walk, std::uint8_t code) {
	if (code != kMarkerSos) {
		return false;
	}
	walk.sawSos = true;
	const auto entropy = scanEntropyData(reader, walk.pos);
	if (!entropy.hasValue()) {
		return entropy.error();
	}
	return continueAfterEntropy(walk, entropy.value());
}

// Skips a length-prefixed segment and, for SOS, its entropy data too.
Result<bool> applySegmentMarker(ByteReader& reader, Walk& walk, std::uint8_t code) {
	const auto afterSegment = skipSegment(reader, walk.pos);
	if (!afterSegment.hasValue()) {
		return afterSegment.error();
	}
	walk.pos = afterSegment.value();
	return applyEntropyIfSos(reader, walk, code);
}

// Applies one marker to the walk; true = EOI reached (walk.pos just past it).
Result<bool> applyMarker(ByteReader& reader, Walk& walk, std::uint8_t code) {
	if (code == kMarkerEoi) {
		return true;
	}
	if (isStandalone(code)) {
		return false;
	}
	return applySegmentMarker(reader, walk, code);
}

// Reads one marker code and applies it; folds a read failure into the
// same typed-error channel applyMarker already uses for structural breaks.
Result<bool> advanceOneMarker(ByteReader& reader, Walk& walk) {
	auto code = readMarkerCode(reader, walk.pos);
	if (!code.hasValue()) {
		return code.error();
	}
	return applyMarker(reader, walk, code.value());
}

// The outcome to report when the walk cannot continue (truncation or a
// structural break) — never reached EOI, whatever SOS state was seen so far.
JpegWalkOutcome brokenAt(const Walk& walk) {
	return JpegWalkOutcome{.end = walk.pos, .sawSos = walk.sawSos, .reachedEoi = false};
}

// One step of the marker walk: a value means the walk is finished (broken
// or EOI reached); std::nullopt means keep looping.
std::optional<JpegWalkOutcome> stepWalk(ByteReader& reader, Walk& walk) {
	const auto finished = advanceOneMarker(reader, walk);
	if (!finished.hasValue()) {
		return brokenAt(walk);
	}
	if (!finished.value()) {
		return std::nullopt;
	}
	return JpegWalkOutcome{.end = walk.pos, .sawSos = walk.sawSos, .reachedEoi = true};
}

} // namespace

JpegWalkOutcome walkJpegMarkers(ByteReader& reader) {
	Walk walk;
	for (;;) {
		auto outcome = stepWalk(reader, walk);
		if (outcome.has_value()) {
			return outcome.value();
		}
	}
}

} // namespace revenant::carve
