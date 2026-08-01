// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery/DestinationRule.hpp"

#include <filesystem>
#include <optional>
#include <system_error>

#include "core/PathPrefix.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"
#include "revenant/core/io/SourceDevice.hpp"

namespace revenant::recovery {

namespace {

// Two spellings of one directory are one directory — and, on both platforms,
// so are a junction or symlink and what it points at. Resolving once up front
// is what keeps the two tiers judging the same place: the storage tier asks the
// OS which volume holds a path, and neither `GetVolumePathNameW` nor a mount
// table follows a link that a mere spelling comparison would have seen through.
[[nodiscard]] std::filesystem::path resolved(const std::filesystem::path& path) {
	std::error_code failed;
	auto real = std::filesystem::weakly_canonical(path, failed);
	return failed ? path : real;
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
	const auto where = resolved(destination);
	// Whatever the source turns out to be, the output tree must not grow around
	// it. Against a device this never fires — a raw device path lies under no
	// directory — which is the whole reason the second tier exists.
	if (startsPath(where, resolved(source))) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	if (classifySource(source) != SourceKind::kDevice) {
		return std::nullopt;
	}
	return refuseOverlap(storageOf(source), storageUnder(where));
}

} // namespace revenant::recovery
