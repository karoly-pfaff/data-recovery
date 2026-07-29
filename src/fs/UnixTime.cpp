// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/UnixTime.hpp"

#include <cstdint>

namespace revenant::fs {

namespace {

// Seconds from the FILETIME epoch (1601-01-01) to the Unix epoch, and ticks per
// second.
constexpr std::int64_t kUnixEpochInFiletimeSeconds = 11'644'473'600;
constexpr std::uint64_t kTicksPerSecond = 10'000'000;

} // namespace

std::uint64_t filetimeFromUnixSeconds(std::int64_t seconds) noexcept {
	if (seconds == 0) {
		return 0;
	}
	return static_cast<std::uint64_t>(seconds + kUnixEpochInFiletimeSeconds) * kTicksPerSecond;
}

} // namespace revenant::fs
