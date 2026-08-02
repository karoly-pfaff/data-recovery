// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace revenant {

// Machine-readable failure category. Extend only when a story needs it (YAGNI).
enum class ErrorCode : std::uint8_t {
	kOutOfRange,       // access past the end of a bounded byte range
	kOverflow,         // offset/length arithmetic would overflow
	kInvalidArgument,  // a caller-supplied parameter is unusable
	kNotFound,         // a named resource (path) does not exist
	kIoFailure,        // an OS-level read fault
	kPermissionDenied, // the OS refused for want of privilege, not for want of the thing
	// a path that can only be read as files, where raw blocks are needed (ADR-0007)
	kNotBlockAddressable,
	// the destination occupies storage the source reads, or the check could not
	// prove otherwise; writing there would overwrite what is being recovered
	kDestinationOnSource,
	// the source stopped answering: not a bad patch but a device that has gone.
	// `offset` is where the unbroken run of refusals began — where the device
	// stopped answering, not where this build stopped believing it
	kSourceLost,
	// the destination or the session has no room left. Distinct from kIoFailure
	// because it is the one write failure an operator can act on, and because
	// every further write against it is known futile
	kStorageExhausted,
};

// A typed error value. `offset` and `osCode` stay 0 unless the failure has a
// meaningful device offset / OS error number to report.
struct Error {
	ErrorCode code{};
	std::uint64_t offset = 0;
	std::int32_t osCode = 0;

	friend bool operator==(const Error&, const Error&) = default;
};

} // namespace revenant
