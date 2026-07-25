// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace revenant {

// Machine-readable failure category. Extend only when a story needs it (YAGNI).
enum class ErrorCode : std::uint8_t {
    kOutOfRange,      // access past the end of a bounded byte range
    kOverflow,        // offset/length arithmetic would overflow
    kInvalidArgument, // a caller-supplied parameter is unusable
    kNotFound,        // a named resource (path) does not exist
    kIoFailure,       // an OS-level read fault
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
