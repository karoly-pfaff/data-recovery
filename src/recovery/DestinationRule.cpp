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
#include "revenant/recovery/UnverifiedIdentity.hpp"

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

// The unanswerable question, under its own code. One code used to cover this and
// the proven conflict both, and the comment there said why: the operator's next
// step was the same either way. It no longer is — this one they can answer, and
// a refusal that cannot say so leaves an encrypted disk unrecoverable.
[[nodiscard]] std::optional<Error> unresolvedIdentity(
	const Result<StorageExtents>& source,
	const Result<StorageExtents>& destination,
	UnverifiedIdentity unverified) {
	if (unverified == UnverifiedIdentity::kAllow) {
		return std::nullopt;
	}
	const Error& reason = source.hasValue() ? destination.error() : source.error();
	return Error{
		.code = ErrorCode::kDestinationIdentityUnresolved,
		.offset = 0,
		.osCode = reason.osCode};
}

} // namespace

std::optional<Error> refuseOverlap(
	const Result<StorageExtents>& source,
	const Result<StorageExtents>& destination,
	UnverifiedIdentity unverified) {
	if (!source.hasValue() || !destination.hasValue()) {
		return unresolvedIdentity(source, destination, unverified);
	}
	// Proven, and nothing overrides it. `unverified` is deliberately not read
	// here: if the tool can show the destination sits on the source, the
	// operator's assertion that they checked is simply wrong.
	if (overlaps(source.value(), destination.value())) {
		return Error{.code = ErrorCode::kDestinationOnSource};
	}
	return std::nullopt;
}

std::optional<Error> destinationOnSource(
	const std::filesystem::path& destination,
	const std::filesystem::path& source,
	UnverifiedIdentity unverified) {
	const auto where = resolved(destination);
	// Whatever the source turns out to be, the output tree must not grow around
	// it. Against a device this never fires — a raw device path lies under no
	// directory — which is the whole reason the second tier exists. `unverified`
	// is not read here: this tier answers from spelling alone and has nothing
	// unresolvable about it, so there is no question for an operator to settle.
	if (startsPath(where, resolved(source))) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	if (classifySource(source) != SourceKind::kDevice) {
		return std::nullopt;
	}
	return refuseOverlap(storageOf(source), storageUnder(where), unverified);
}

} // namespace revenant::recovery
