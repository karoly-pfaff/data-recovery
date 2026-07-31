// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery/DestinationRule.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <system_error>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"
#include "revenant/core/io/SourceDevice.hpp"

namespace revenant::recovery {

namespace {

// A destination that contains the source is refused outright. Both sides are
// canonicalized first: two spellings of one directory are one directory.
[[nodiscard]] bool
contains(const std::filesystem::path& outer, const std::filesystem::path& inner) {
	std::error_code failed;
	const auto root = std::filesystem::weakly_canonical(outer, failed);
	const auto candidate = std::filesystem::weakly_canonical(inner, failed);
	const auto reach = std::ranges::mismatch(root, candidate);
	return reach.in1 == root.end();
}

} // namespace

std::optional<Error>
refuseOverlap(const Result<StorageExtents>& source, const Result<StorageExtents>& destination) {
	// One code covers both the proven conflict and the unanswerable question:
	// the operator's next step is the same either way, and reporting an
	// unresolved identity as an I/O failure would name a read that never
	// happened. The OS's reason rides along when there was one.
	if (!source.hasValue() || !destination.hasValue()) {
		const Error& reason = source.hasValue() ? destination.error() : source.error();
		return Error{.code = ErrorCode::kDestinationOnSource, .offset = 0, .osCode = reason.osCode};
	}
	if (overlaps(source.value(), destination.value())) {
		return Error{.code = ErrorCode::kDestinationOnSource};
	}
	return std::nullopt;
}

std::optional<Error>
destinationOnSource(const std::filesystem::path& destination, const std::filesystem::path& source) {
	// Whatever the source turns out to be, the output tree must not grow around
	// it. Against a device this never fires — a raw device path lies under no
	// directory — which is the whole reason the second tier exists.
	if (contains(destination, source)) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	if (classifySource(source) != SourceKind::kDevice) {
		return std::nullopt;
	}
	return refuseOverlap(storageOf(source), storageUnder(destination));
}

} // namespace revenant::recovery
