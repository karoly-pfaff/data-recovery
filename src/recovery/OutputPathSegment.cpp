// SPDX-License-Identifier: GPL-3.0-or-later
#include "OutputPathSegment.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "revenant/core/BoundedCount.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/recovery/OutputPath.hpp"

namespace revenant::recovery {

namespace {

constexpr std::array<std::string_view, 22> kReservedBasenames{
	"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
	"COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
};

// Whether a raw segment, once split out, should be dropped silently, kept
// (and cleaned further), or rejected outright.
enum class Disposition : std::uint8_t { kSkip, kKeep };

// The position just past the current segment: the next '/' or '\', or the
// end of the string if this is the last segment.
std::size_t segmentEnd(std::string_view name, std::size_t start) {
	const std::size_t separatorPos = name.find_first_of("/\\", start);
	return separatorPos == std::string_view::npos ? name.size() : separatorPos;
}

// Raw segments between '/'/'\' separators; may include empty ones (e.g. a
// leading, trailing, or doubled separator) and "." — filtered by the caller.
std::vector<std::string_view> splitRawSegments(std::string_view name) {
	std::vector<std::string_view> segments;
	std::size_t start = 0;
	while (start <= name.size()) {
		const std::size_t end = segmentEnd(name, start);
		segments.push_back(name.substr(start, end - start));
		start = end + 1;
	}
	return segments;
}

bool looksLikeDrivePrefix(std::string_view segment) {
	return segment.size() >= 2 && std::isalpha(static_cast<unsigned char>(segment.front())) != 0 &&
		   segment.at(1) == ':';
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
	auto toLower = [](char ch) { return std::tolower(static_cast<unsigned char>(ch)); };
	return a.size() == b.size() && std::ranges::equal(a, b, {}, toLower, toLower);
}

bool isReservedBasename(std::string_view basename) {
	return std::ranges::any_of(kReservedBasenames, [basename](std::string_view reserved) {
		return equalsIgnoreCase(basename, reserved);
	});
}

// Windows-reserved names are matched by the basename before the first dot
// ("CON.jpg" and "con" both match "CON"; "CONX" does not).
std::string neutralizeReservedName(std::string segment) {
	const std::string_view basename = std::string_view{segment}.substr(0, segment.find('.'));
	if (!isReservedBasename(basename)) {
		return segment;
	}
	return "_" + segment;
}

std::string stripTrailingDotsAndSpaces(std::string segment) {
	while (!segment.empty() && (segment.back() == '.' || segment.back() == ' ')) {
		segment.pop_back();
	}
	if (!segment.empty()) {
		return segment;
	}
	return "_";
}

// Strip trailing dots/spaces BEFORE neutralizing the reserved basename: a
// name like "CON " (no dot) never equals "CON" under the equal-length
// case-insensitive compare, so neutralizing first would let the trailing
// strip peel the space back off afterward and leave the bare reserved name
// ("CON") on disk — the exact ADR-0009 rule-2 bypass this order prevents.
Result<std::string> cleanSegment(std::string_view rawSegment) {
	std::string cleaned =
		neutralizeReservedName(stripTrailingDotsAndSpaces(std::string{rawSegment}));
	if (!boundedCount(cleaned.size(), kMaxSegmentBytes).hasValue()) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	return cleaned;
}

// Structural classification only: does this raw segment get silently
// dropped, rejected outright (traversal, drive prefix), or does it go on to
// `cleanSegment`?
Result<Disposition> classifyRawSegment(std::string_view rawSegment) {
	if (rawSegment.empty() || rawSegment == ".") {
		return Disposition::kSkip;
	}
	if (rawSegment == ".." || looksLikeDrivePrefix(rawSegment)) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	return Disposition::kKeep;
}

// Cleans `rawSegment` and appends it to `segments` — the caller has already
// classified it as kKeep.
Result<std::monostate>
keepCleanedSegment(std::vector<std::string>& segments, std::string_view rawSegment) {
	const auto cleaned = cleanSegment(rawSegment);
	if (!cleaned.hasValue()) {
		return cleaned.error();
	}
	segments.push_back(cleaned.value());
	return std::monostate{};
}

// Folds one raw segment into `segments`: nothing (dropped), an appended
// cleaned segment (kept), or a typed error propagated from classification
// or cleaning.
Result<std::monostate>
absorbRawSegment(std::vector<std::string>& segments, std::string_view rawSegment) {
	const auto disposition = classifyRawSegment(rawSegment);
	if (!disposition.hasValue()) {
		return disposition.error();
	}
	if (disposition.value() == Disposition::kSkip) {
		return std::monostate{};
	}
	return keepCleanedSegment(segments, rawSegment);
}

Result<std::vector<std::string>> foldSegments(std::string_view name) {
	std::vector<std::string> segments;
	for (const std::string_view rawSegment : splitRawSegments(name)) {
		const auto outcome = absorbRawSegment(segments, rawSegment);
		if (!outcome.hasValue()) {
			return outcome.error();
		}
	}
	return segments;
}

Result<std::vector<std::string>> checkSegmentCount(std::vector<std::string> segments) {
	if (!boundedCount(segments.size(), kMaxSegments).hasValue()) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	return segments;
}

} // namespace

Result<std::vector<std::string>> collectSegments(std::string_view name) {
	const auto folded = foldSegments(name);
	if (!folded.hasValue()) {
		return folded.error();
	}
	return checkSegmentCount(folded.value());
}

} // namespace revenant::recovery
