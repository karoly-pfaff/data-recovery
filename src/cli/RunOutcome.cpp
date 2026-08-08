// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RunOutcome.hpp"

#include "revenant/core/Error.hpp"

namespace revenant::cli {

RunOutcome outcomeOf(ErrorCode code) noexcept {
	switch (code) {
	// Every one of these is about what the run was pointed at, and every one is
	// answered by changing an argument. Nothing was produced.
	case ErrorCode::kNotFound:
	case ErrorCode::kInvalidArgument:
	case ErrorCode::kDestinationOnSource:
	case ErrorCode::kDestinationIdentityUnresolved:
	case ErrorCode::kNotBlockAddressable:
	case ErrorCode::kPermissionDenied:
		return RunOutcome::kCouldNotStart;
	// The device went away. What was found is on disk and the checkpoint stands,
	// so the same command picks it up — there is nothing for the operator to fix
	// except, perhaps, the enclosure.
	case ErrorCode::kSourceLost:
		return RunOutcome::kStoppedResumable;
	// Room is what is missing, and re-running before it is found would fail the
	// same way.
	case ErrorCode::kStorageExhausted:
		return RunOutcome::kStoppedNeedsAttention;
	// A fault the layers below could not classify further. It stopped a run that
	// had already started, and it is not known to be resumable, so it is the
	// row that asks someone to look before re-running.
	case ErrorCode::kIoFailure:
	case ErrorCode::kOutOfRange:
	case ErrorCode::kOverflow:
		break;
	}
	return RunOutcome::kStoppedNeedsAttention;
}

} // namespace revenant::cli
