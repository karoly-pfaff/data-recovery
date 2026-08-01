// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/Utf16Name.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "core/NameEscape.hpp"
#include "revenant/core/Endian.hpp"

namespace revenant {

namespace {

using CodeUnit = std::uint16_t;

constexpr CodeUnit kHighSurrogateStart = 0xD800;
constexpr CodeUnit kHighSurrogateEnd = 0xDBFF;
constexpr CodeUnit kLowSurrogateStart = 0xDC00;
constexpr CodeUnit kLowSurrogateEnd = 0xDFFF;
constexpr CodeUnit kNul = 0x0000;
constexpr char32_t kSupplementaryPlaneStart = 0x10000;

bool isHighSurrogate(CodeUnit unit) {
	return unit >= kHighSurrogateStart && unit <= kHighSurrogateEnd;
}

bool isLowSurrogate(CodeUnit unit) {
	return unit >= kLowSurrogateStart && unit <= kLowSurrogateEnd;
}

// A code unit that decodeStep escapes on its own, without ever consulting a
// neighbor: a literal NUL (never allowed to survive as a raw byte) or a low
// surrogate with no preceding high surrogate (a reversed pair).
bool isDirectlyEscapable(CodeUnit unit) {
	return unit == kNul || isLowSurrogate(unit);
}

// The fixed input plus its code-unit count, threaded through the decode
// steps below so no function needs more than a handful of parameters.
struct DecodeContext {
	std::span<const std::byte> utf16le;
	std::size_t unitCount = 0;
};

CodeUnit unitAt(const DecodeContext& ctx, std::size_t unitIndex) {
	return fromLittleEndian<CodeUnit>(ctx.utf16le.subspan(unitIndex * 2).first<2>());
}

char32_t combineSurrogatePair(CodeUnit high, CodeUnit low) {
	const auto highBits = static_cast<char32_t>(high - kHighSurrogateStart) << 10U;
	const auto lowBits = static_cast<char32_t>(low - kLowSurrogateStart);
	return kSupplementaryPlaneStart + highBits + lowBits;
}

void appendContinuationByte(std::string& out, char32_t codePoint, unsigned shift) {
	out.push_back(static_cast<char>(0x80U | ((codePoint >> shift) & 0x3FU)));
}

void appendUtf8OneByte(std::string& out, char32_t codePoint) {
	out.push_back(static_cast<char>(codePoint));
}

void appendUtf8TwoByte(std::string& out, char32_t codePoint) {
	out.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
	appendContinuationByte(out, codePoint, 0U);
}

void appendUtf8ThreeByte(std::string& out, char32_t codePoint) {
	out.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
	appendContinuationByte(out, codePoint, 6U);
	appendContinuationByte(out, codePoint, 0U);
}

void appendUtf8FourByte(std::string& out, char32_t codePoint) {
	out.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
	appendContinuationByte(out, codePoint, 12U);
	appendContinuationByte(out, codePoint, 6U);
	appendContinuationByte(out, codePoint, 0U);
}

// Encodes one Unicode code point (never a lone surrogate - callers only
// reach this with either a plain BMP unit or an already-combined
// surrogate-pair code point) as 1/2/3/4 UTF-8 bytes by value.
void appendUtf8CodePoint(std::string& out, char32_t codePoint) {
	if (codePoint < 0x80) {
		appendUtf8OneByte(out, codePoint);
	} else if (codePoint < 0x800) {
		appendUtf8TwoByte(out, codePoint);
	} else if (codePoint < 0x10000) {
		appendUtf8ThreeByte(out, codePoint);
	} else {
		appendUtf8FourByte(out, codePoint);
	}
}

// Accumulates the decoded UTF-8 text and whether every code unit decoded
// losslessly (false as soon as any escape is emitted).
struct DecodeState {
	std::string utf8;
	bool lossless = true;
};

void appendCodePoint(DecodeState& state, char32_t codePoint) {
	appendUtf8CodePoint(state.utf8, codePoint);
}

void appendEscapedUnit(DecodeState& state, CodeUnit unit) {
	appendEscapedCodeUnit(state.utf8, unit);
	state.lossless = false;
}

void appendTrailingByte(DecodeState& state, std::byte raw) {
	appendEscapedByte(state.utf8, raw);
	state.lossless = false;
}

// Resolves a high surrogate at `unitIndex`: a following low surrogate pairs
// into one 4-byte code point (consumes 2 units); anything else (no next
// unit, or a non-low-surrogate next unit) is an unpaired surrogate, escaped
// on its own (consumes 1 unit).
std::size_t decodeHighSurrogate(
	DecodeState& state,
	const DecodeContext& ctx,
	std::size_t unitIndex,
	CodeUnit high) {
	const bool hasNext = unitIndex + 1 < ctx.unitCount;
	const CodeUnit low = hasNext ? unitAt(ctx, unitIndex + 1) : CodeUnit{0};
	if (hasNext && isLowSurrogate(low)) {
		appendCodePoint(state, combineSurrogatePair(high, low));
		return 2;
	}
	appendEscapedUnit(state, high);
	return 1;
}

// Decodes the code unit at `unitIndex`, appending to `state`; returns how
// many code units it consumed (1, or 2 for a valid surrogate pair).
std::size_t decodeStep(DecodeState& state, const DecodeContext& ctx, std::size_t unitIndex) {
	const CodeUnit unit = unitAt(ctx, unitIndex);
	if (isDirectlyEscapable(unit)) {
		appendEscapedUnit(state, unit);
		return 1;
	}
	if (isHighSurrogate(unit)) {
		return decodeHighSurrogate(state, ctx, unitIndex, unit);
	}
	appendCodePoint(state, unit);
	return 1;
}

void decodeFullUnits(DecodeState& state, const DecodeContext& ctx) {
	std::size_t unitIndex = 0;
	while (unitIndex < ctx.unitCount) {
		unitIndex += decodeStep(state, ctx, unitIndex);
	}
}

} // namespace

DecodedName decodeUtf16Name(std::span<const std::byte> utf16le) {
	DecodeState state;
	const DecodeContext ctx{.utf16le = utf16le, .unitCount = utf16le.size() / 2};
	decodeFullUnits(state, ctx);
	if (utf16le.size() % 2 != 0) {
		appendTrailingByte(state, utf16le.back());
	}
	return DecodedName{.utf8 = std::move(state.utf8), .lossless = state.lossless};
}

} // namespace revenant
