// SPDX-License-Identifier: GPL-3.0-or-later
#include "JpegEntropyScan.hpp"

#include <cstdint>
#include <optional>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

namespace {

constexpr std::uint8_t kMarkerPrefix = 0xFF;
constexpr std::uint8_t kStuffedZero = 0x00;
constexpr std::uint8_t kMarkerRstFirst = 0xD0;
constexpr std::uint8_t kMarkerRstLast = 0xD7;
constexpr std::uint64_t kMarkerPairBytes = 2;

bool isRestartMarker(std::uint8_t code) {
	return code >= kMarkerRstFirst && code <= kMarkerRstLast;
}

// Having seen an 0xFF at `pos`, decides whether it's a stuffed byte or an
// RST marker (data continues, pos advances past the pair), a genuine
// terminator (stop; pos stays on the 0xFF), or the buffer ran out before
// the disambiguating byte could be read (stop; pos stays on the 0xFF,
// since that byte's role could never be confirmed).
std::optional<EntropyOutcome> classifyMarkerPair(ByteReader& reader, std::uint64_t& pos) {
	const auto code = reader.readLe<std::uint8_t>(pos + 1);
	if (!code.hasValue()) {
		return EntropyOutcome{.pos = pos, .foundTerminator = false};
	}
	if (code.value() != kStuffedZero && !isRestartMarker(code.value())) {
		return EntropyOutcome{.pos = pos, .foundTerminator = true};
	}
	pos += kMarkerPairBytes;
	return std::nullopt;
}

// One entropy step: std::nullopt while still inside entropy data (pos
// advanced); a value once the step is done (see EntropyOutcome).
std::optional<EntropyOutcome> advanceEntropyStep(ByteReader& reader, std::uint64_t& pos) {
	const auto byte = reader.readLe<std::uint8_t>(pos);
	if (!byte.hasValue()) {
		return EntropyOutcome{.pos = pos, .foundTerminator = false};
	}
	if (byte.value() != kMarkerPrefix) {
		++pos;
		return std::nullopt;
	}
	return classifyMarkerPair(reader, pos);
}

} // namespace

Result<EntropyOutcome> scanEntropyData(ByteReader& reader, std::uint64_t pos) {
	for (;;) {
		const auto step = advanceEntropyStep(reader, pos);
		if (step.has_value()) {
			return step.value();
		}
	}
}

} // namespace revenant::carve
