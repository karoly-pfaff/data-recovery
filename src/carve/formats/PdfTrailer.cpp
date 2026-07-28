// SPDX-License-Identifier: GPL-3.0-or-later
#include "PdfTrailer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "AsciiText.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

namespace {

constexpr std::uint64_t kEofMarkerBytes = 5;
// A `startxref` line is a keyword, a line break, and a decimal offset; looking
// this far back from `%%EOF` covers it with room to spare, and bounds the
// search (ADR-0009) instead of scanning the whole file backwards.
constexpr std::size_t kTrailerLookbackBytes = 64;
constexpr std::size_t kResolveSampleBytes = 8;
constexpr std::string_view kStartxref = "startxref";
constexpr std::string_view kXrefKeyword = "xref";
constexpr std::string_view kDigits = "0123456789";
constexpr std::byte kCarriageReturn{'\r'};
constexpr std::byte kLineFeed{'\n'};

constexpr std::array<std::byte, 5>
	kEofMarker{std::byte{'%'}, std::byte{'%'}, std::byte{'E'}, std::byte{'O'}, std::byte{'F'}};

// The offset of the last `%%EOF`. The *last* one matters: an incrementally
// saved PDF carries one per revision, and the first ends the oldest revision.
[[nodiscard]] Result<std::uint64_t> lastEofOffset(const ByteReader& reader) {
	const auto all = reader.bytes(0, reader.size());
	if (!all.hasValue()) {
		return Error{.code = ErrorCode::kNotFound, .offset = 0};
	}
	const auto found = std::ranges::find_end(all.value(), kEofMarker);
	if (found.empty()) {
		return Error{.code = ErrorCode::kNotFound, .offset = 0};
	}
	return static_cast<std::uint64_t>(std::distance(all.value().begin(), found.begin()));
}

// PDF allows `%%EOF` to be followed by a line ending, which belongs to the
// file. Anything else after it does not.
[[nodiscard]] std::uint64_t lineEndBytes(const ByteReader& reader, std::uint64_t after) {
	const auto pair = reader.bytes(after, 2);
	if (pair.hasValue() && pair.value().front() == kCarriageReturn &&
		pair.value().back() == kLineFeed) {
		return 2;
	}
	const auto one = reader.bytes(after, 1);
	const auto isBreak = one.hasValue() && (one.value().front() == kCarriageReturn ||
											one.value().front() == kLineFeed);
	return isBreak ? 1 : 0;
}

// std::from_chars's [first, last) pointer pair is the only portable overload
// here; the arithmetic stays inside one already-bounded std::string.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
[[nodiscard]] Result<std::uint64_t> parseDigitsAfter(const std::string& text, std::size_t from) {
	const auto at = text.find_first_of(kDigits, from);
	if (at == std::string::npos) {
		return Error{.code = ErrorCode::kNotFound, .offset = from};
	}
	std::uint64_t value = 0;
	const auto [end, err] = std::from_chars(text.data() + at, text.data() + text.size(), value);
	if (err != std::errc{}) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = at};
	}
	return value;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

// The offset on the `startxref` line preceding the end marker.
[[nodiscard]] Result<std::uint64_t>
startxrefOffset(const ByteReader& reader, std::uint64_t marker) {
	const auto from = marker > kTrailerLookbackBytes ? marker - kTrailerLookbackBytes : 0;
	const auto window = reader.bytes(from, static_cast<std::size_t>(marker - from));
	if (!window.hasValue()) {
		return Error{.code = ErrorCode::kNotFound, .offset = marker};
	}
	const auto text = asciiText(window.value());
	const auto at = text.rfind(kStartxref);
	return at == std::string::npos ? Result<std::uint64_t>(Error{.code = ErrorCode::kNotFound})
								   : parseDigitsAfter(text, at);
}

// A cross reference lives either in a classic `xref` table or in an indirect
// object (`N G obj`) holding a cross-reference stream. Either one proves the
// offset points at real structure rather than into the middle of a byte run.
[[nodiscard]] bool resolvesAt(const ByteReader& reader, std::uint64_t offset) {
	const auto head = reader.bytes(offset, kResolveSampleBytes);
	if (!head.hasValue()) {
		return false;
	}
	const auto text = asciiText(head.value());
	return text.starts_with(kXrefKeyword) || kDigits.find(text.front()) != std::string_view::npos;
}

[[nodiscard]] bool crossReferenceResolves(const ByteReader& reader, std::uint64_t marker) {
	const auto offset = startxrefOffset(reader, marker);
	return offset.hasValue() && resolvesAt(reader, offset.value());
}

} // namespace

Result<PdfTrailer> findPdfTrailer(const ByteReader& reader) {
	return lastEofOffset(reader).map([&reader](std::uint64_t marker) {
		const auto afterMarker = marker + kEofMarkerBytes;
		return PdfTrailer{
			.end = afterMarker + lineEndBytes(reader, afterMarker),
			.crossReferenceResolves = crossReferenceResolves(reader, marker)};
	});
}

} // namespace revenant::carve
