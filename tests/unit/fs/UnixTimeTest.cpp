// SPDX-License-Identifier: GPL-3.0-or-later
// story-0034: Unix seconds into the layer's one epoch. ext4 states every
// timestamp this way. The tick counts are computed independently of the code
// under test; the zero case pins down that an unset field yields no timestamp
// rather than a plausible-looking 1970.
#include "fs/UnixTime.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using revenant::fs::filetimeFromUnixSeconds;

// The Unix epoch itself: 11 644 473 600 s after FILETIME's, at 10^7 ticks each.
constexpr std::uint64_t kUnixEpochTicks = 116'444'736'000'000'000ULL;

// 2020-08-01 12:00:00 UTC = 1 596 283 200 s after the Unix epoch.
constexpr std::int64_t kKnownSeconds = 1'596'283'200;
constexpr std::uint64_t kKnownTicks = 132'407'568'000'000'000ULL;

// The largest value ext4's 32-bit `i_mtime` can hold — 2106-02-07 06:28:15 UTC.
constexpr std::int64_t kMaxSeconds = 4'294'967'295;
constexpr std::uint64_t kMaxTicks = 159'394'408'950'000'000ULL;

TEST(UnixTime, OneSecondPastTheEpochIsItsKnownFiletime) {
	EXPECT_EQ(filetimeFromUnixSeconds(1), kUnixEpochTicks + 10'000'000ULL);
}

TEST(UnixTime, AKnownDateConvertsToItsKnownFiletime) {
	EXPECT_EQ(filetimeFromUnixSeconds(kKnownSeconds), kKnownTicks);
}

// ext4's timestamp field is 32 bits wide, so this is as far as it goes without
// the extra-precision bits no volume this build reads uses.
TEST(UnixTime, TheLargestThirtyTwoBitSecondConvertsWithoutOverflowing) {
	EXPECT_EQ(filetimeFromUnixSeconds(kMaxSeconds), kMaxTicks);
}

// A field that was never set reads as zero on disk, and the layer spells "no
// timestamp" the same way. Handing back the Unix epoch would be a fabrication.
TEST(UnixTime, ZeroSecondsIsNoTimestampRatherThanNineteenSeventy) {
	EXPECT_EQ(filetimeFromUnixSeconds(0), 0U);
}

} // namespace
