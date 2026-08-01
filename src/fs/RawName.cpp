// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <utility>

#include "core/NameEscape.hpp"
#include "fs/PathSafeByte.hpp"
#include "revenant/core/Utf16Name.hpp"
#include "revenant/fs/NameDecode.hpp"

namespace revenant::fs {

namespace {

// The accumulating name and whether every byte so far survived as itself.
struct RawState {
	std::string utf8;
	bool lossless = true;
};

[[nodiscard]] unsigned valueOf(std::byte raw) noexcept {
	return std::to_integer<unsigned>(raw);
}

// How many bytes a well-formed sequence starting with `lead` occupies, or 0 when
// that byte cannot start one at all. `0xC0` and `0xC1` are missing on purpose:
// they can only ever spell an overlong two-byte encoding.
[[nodiscard]] std::size_t sequenceLength(unsigned lead) noexcept {
	if (lead < 0x80U) {
		return 1;
	}
	if (lead >= 0xC2U && lead <= 0xDFU) {
		return 2;
	}
	if (lead >= 0xE0U && lead <= 0xEFU) {
		return 3;
	}
	return (lead >= 0xF0U && lead <= 0xF4U) ? 4 : 0;
}

// What the *second* byte of a sequence may be. Every byte after it is a plain
// continuation byte, `0x80` to `0xBF`.
struct SecondByte {
	unsigned low;
	unsigned high;
};

constexpr SecondByte kAnyContinuation{.low = 0x80U, .high = 0xBFU};

// The four leads whose second byte is narrower than that, and what each narrows
// it to. Between them they rule out UTF-8's remaining illegal encodings: an
// overlong three- or four-byte form (`0xE0`, `0xF0`), a surrogate (`0xED`), and
// a code point past U+10FFFF (`0xF4`).
struct NarrowLead {
	unsigned lead;
	SecondByte range;
};

constexpr std::array<NarrowLead, 4> kNarrowLeads{
	NarrowLead{.lead = 0xE0U, .range = {.low = 0xA0U, .high = 0xBFU}},
	NarrowLead{.lead = 0xEDU, .range = {.low = 0x80U, .high = 0x9FU}},
	NarrowLead{.lead = 0xF0U, .range = {.low = 0x90U, .high = 0xBFU}},
	NarrowLead{.lead = 0xF4U, .range = {.low = 0x80U, .high = 0x8FU}}};

// Searched by walking rather than through an iterator: libstdc++ makes
// `std::array`'s iterator a raw pointer and the MSVC STL makes it a class, so no
// single spelling of the `auto` holding one satisfies both toolchains' lint.
[[nodiscard]] SecondByte secondByteRange(unsigned lead) noexcept {
	for (const NarrowLead& narrow : kNarrowLeads) {
		if (narrow.lead == lead) {
			return narrow.range;
		}
	}
	return kAnyContinuation;
}

[[nodiscard]] bool isContinuation(std::byte raw) noexcept {
	return (valueOf(raw) & 0xC0U) == 0x80U;
}

// Whether a multi-byte sequence of exactly `length` bytes stands at the front of
// `at`. A sequence the input ends inside is not well-formed: it is a truncation,
// and truncations are escaped rather than completed with a guess.
[[nodiscard]] bool sequenceIsWellFormed(std::span<const std::byte> at, std::size_t length) {
	if (at.size() < length) {
		return false;
	}
	const auto range = secondByteRange(valueOf(at.front()));
	const auto second = valueOf(at.subspan(1).front());
	return second >= range.low && second <= range.high &&
		   std::ranges::all_of(at.subspan(2, length - 2), isContinuation);
}

void appendEscaped(RawState& state, std::byte raw) {
	appendEscapedByte(state.utf8, raw);
	state.lossless = false;
}

void appendVerbatim(RawState& state, std::span<const std::byte> sequence) {
	for (const std::byte raw : sequence) {
		state.utf8.push_back(static_cast<char>(valueOf(raw)));
	}
}

// One byte that is not part of a well-formed multi-byte sequence: either plain
// ASCII, which passes through unless ADR-0010 reserves it, or a byte that cannot
// begin a sequence at all, which is escaped outright.
void appendSingleByte(RawState& state, std::byte raw, bool isAscii) {
	if (isAscii && passesThroughAsItself(raw)) {
		state.utf8.push_back(static_cast<char>(valueOf(raw)));
		return;
	}
	appendEscaped(state, raw);
}

// One step over the front of `at`, returning how many bytes it consumed. A byte
// that cannot begin a well-formed sequence costs exactly itself, so the rest of
// the name survives a single bad byte in the middle of it.
[[nodiscard]] std::size_t decodeStep(RawState& state, std::span<const std::byte> at) {
	const auto length = sequenceLength(valueOf(at.front()));
	if (length > 1 && sequenceIsWellFormed(at, length)) {
		appendVerbatim(state, at.first(length));
		return length;
	}
	appendSingleByte(state, at.front(), length == 1);
	return 1;
}

} // namespace

DecodedName decodeRawName(std::span<const std::byte> raw) {
	RawState state;
	std::size_t at = 0;
	while (at < raw.size()) {
		at += decodeStep(state, raw.subspan(at));
	}
	return DecodedName{.utf8 = std::move(state.utf8), .lossless = state.lossless};
}

} // namespace revenant::fs
