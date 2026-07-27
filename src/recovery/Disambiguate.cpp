// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/Disambiguate.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace revenant::recovery {

namespace {

// `desired` split at its LAST dot: `extension` keeps the dot itself (or is
// empty for an extensionless name), so re-assembling `stem + suffix +
// extension` always inserts the suffix in the right place.
struct NameParts {
	std::string_view stem;
	std::string_view extension;
};

NameParts splitAtLastDot(std::string_view name) {
	const auto dotPos = name.rfind('.');
	if (dotPos == std::string_view::npos) {
		return NameParts{.stem = name, .extension = {}};
	}
	return NameParts{.stem = name.substr(0, dotPos), .extension = name.substr(dotPos)};
}

std::string numberedCandidate(const NameParts& parts, int attemptNumber) {
	return std::string{parts.stem} + " (" + std::to_string(attemptNumber) + ")" +
		   std::string{parts.extension};
}

// Tries "name (2).ext" .. "name (kMaxDisambiguationAttempts + 1).ext" in
// order, returning the first one `taken` reports free - or nullopt if every
// one of the kMaxDisambiguationAttempts candidates is already taken.
std::optional<std::string> firstFreeNumberedCandidate(
	std::string_view desired,
	const std::function<bool(std::string_view)>& taken) {
	const NameParts parts = splitAtLastDot(desired);
	for (int attemptNumber = 2; attemptNumber <= kMaxDisambiguationAttempts + 1; ++attemptNumber) {
		std::string candidate = numberedCandidate(parts, attemptNumber);
		if (!taken(candidate)) {
			return candidate;
		}
	}
	return std::nullopt;
}

// The unconditional fallback once every numbered candidate is exhausted:
// still deterministic, still derived from `desired`, but no longer checked
// against `taken` - there is no larger bound to retry with.
std::string overflowFallback(std::string_view desired) {
	return std::string{desired} + " (overflow)" + std::to_string(kMaxDisambiguationAttempts);
}

} // namespace

std::string
disambiguate(std::string_view desired, const std::function<bool(std::string_view)>& taken) {
	if (!taken(desired)) {
		return std::string{desired};
	}
	auto candidate = firstFreeNumberedCandidate(desired, taken);
	if (candidate.has_value()) {
		return std::move(candidate).value();
	}
	return overflowFallback(desired);
}

} // namespace revenant::recovery
