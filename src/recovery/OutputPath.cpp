// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/OutputPath.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "OutputPathSegment.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::recovery {

namespace {

bool isForbiddenByte(char ch) {
	const auto byte = static_cast<unsigned char>(ch);
	return byte < 0x20 || byte == 0x7F;
}

bool hasForbiddenBytes(std::string_view name) {
	return std::ranges::any_of(name, isForbiddenByte);
}

// A leading separator ("/etc/x", "\\server\share") would, if merely split
// into segments, silently collapse to a relative join instead of being
// rejected as the escape attempt it is — checked before splitting.
bool startsWithSeparator(std::string_view name) {
	return !name.empty() && (name.front() == '/' || name.front() == '\\');
}

// The two raw-byte rejections that must happen before any segment splitting
// is even attempted.
bool isUsableRawName(std::string_view name) {
	return !hasForbiddenBytes(name) && !startsWithSeparator(name);
}

std::filesystem::path
assembleUnder(const std::filesystem::path& root, const std::vector<std::string>& segments) {
	std::filesystem::path joined = root;
	for (const std::string& segment : segments) {
		joined /= segment;
	}
	return joined;
}

// Component-wise containment: every path element of `root` must appear, in
// order, as a prefix of `candidate`'s elements. A plain string-prefix check
// would wrongly accept a sibling directory ("…/out-evil") as "contained"
// within "…/out"; comparing path elements avoids that.
bool isContainedWithin(const std::filesystem::path& candidate, const std::filesystem::path& root) {
	auto rootIt = root.begin();
	auto candidateIt = candidate.begin();
	for (; rootIt != root.end(); ++rootIt, ++candidateIt) {
		if (candidateIt == candidate.end() || *candidateIt != *rootIt) {
			return false;
		}
	}
	return true;
}

// Joins the cleaned segments under `root` and verifies containment. By
// construction (no ".." or drive-rooted segment ever survives
// `collectSegments`) the join is always lexically inside `root`; this is
// the belt-and-braces check, not the primary defense.
//
// Both sides are canonicalized before comparing. Checking the assembled path,
// which keeps the caller's spelling of the root, against a canonicalized root
// compares two different namings of the same directory: any filesystem alias
// (a symlink or junction, or a Windows 8.3 short name such as "RUNNER~1")
// made every legitimate name fail containment. The returned path keeps the
// caller's spelling — they asked for output there — while the check runs in
// the one form where equality is meaningful.
Result<std::filesystem::path>
confineToRoot(const std::filesystem::path& root, const std::vector<std::string>& segments) {
	const std::filesystem::path joined = assembleUnder(root, segments).lexically_normal();
	if (!isContainedWithin(
			std::filesystem::weakly_canonical(joined),
			std::filesystem::weakly_canonical(root))) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	return joined;
}

// A cleaned-but-empty segment list (the whole name decomposed to nothing —
// e.g. ".", "", "//") is exactly as unusable as a rejected one.
Result<std::filesystem::path> confineNonEmptySegments(
	const std::filesystem::path& root,
	const std::vector<std::string>& segments) {
	if (segments.empty()) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	return confineToRoot(root, segments);
}

} // namespace

Result<std::filesystem::path>
sanitizeOutputPath(const std::filesystem::path& destinationRoot, std::string_view relativeName) {
	if (!isUsableRawName(relativeName)) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	const auto segments = collectSegments(relativeName);
	if (!segments.hasValue()) {
		return segments.error();
	}
	return confineNonEmptySegments(destinationRoot, segments.value());
}

} // namespace revenant::recovery
