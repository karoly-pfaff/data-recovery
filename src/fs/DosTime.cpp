// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/DosTime.hpp"

#include <cstdint>

namespace revenant::fs {

namespace {

// Seconds from the FILETIME epoch (1601-01-01) to the Unix epoch, and ticks
// per second. Every conversion below lands on Unix days first, because that is
// the epoch the civil-date algorithm is written against.
constexpr std::int64_t kUnixEpochInFiletimeSeconds = 11'644'473'600;
constexpr std::uint64_t kTicksPerSecond = 10'000'000;
constexpr std::int64_t kSecondsPerDay = 86'400;
constexpr std::int64_t kDosEpochYear = 1980;

// A DOS date/time pair unpacked into fields, none of them yet judged.
struct Civil {
	std::int64_t year;
	unsigned month;
	unsigned day;
	unsigned hour;
	unsigned minute;
	unsigned second;
};

[[nodiscard]] Civil unpack(DosTimestamp stamp) noexcept {
	return Civil{
		.year = kDosEpochYear + ((stamp.date >> 9U) & 0x7FU),
		.month = (stamp.date >> 5U) & 0x0FU,
		.day = stamp.date & 0x1FU,
		.hour = (stamp.time >> 11U) & 0x1FU,
		.minute = (stamp.time >> 5U) & 0x3FU,
		.second = (stamp.time & 0x1FU) * 2U};
}

[[nodiscard]] bool isExpressible(const Civil& civil) noexcept {
	const bool dateIsReal =
		civil.month >= 1U && civil.month <= 12U && civil.day >= 1U && civil.day <= 31U;
	return dateIsReal && civil.hour <= 23U && civil.minute <= 59U && civil.second <= 59U;
}

// Days from 1970-01-01 to a proleptic Gregorian date (Howard Hinnant's
// `days_from_civil`). Shifting the year to start in March makes the leap day
// the last day of the cycle, which is what removes every special case.
[[nodiscard]] std::int64_t daysFromCivil(const Civil& civil) noexcept {
	const std::int64_t shifted = civil.year - static_cast<std::int64_t>(civil.month <= 2U);
	const std::int64_t era = (shifted >= 0 ? shifted : shifted - 399) / 400;
	const auto yearOfEra = static_cast<std::uint64_t>(shifted - (era * 400));
	const unsigned marchBased = civil.month > 2U ? civil.month - 3U : civil.month + 9U;
	const std::uint64_t dayOfYear = ((((153U * marchBased) + 2U) / 5U) + civil.day) - 1U;
	const std::uint64_t dayOfEra =
		(yearOfEra * 365U) + (yearOfEra / 4U) - (yearOfEra / 100U) + dayOfYear;
	return (era * 146'097) + static_cast<std::int64_t>(dayOfEra) - 719'468;
}

[[nodiscard]] std::int64_t secondsOfDay(const Civil& civil) noexcept {
	const std::int64_t hours = civil.hour;
	const std::int64_t minutes = civil.minute;
	return (hours * 3600) + (minutes * 60) + static_cast<std::int64_t>(civil.second);
}

[[nodiscard]] std::int64_t unixSecondsOf(const Civil& civil) noexcept {
	return (daysFromCivil(civil) * kSecondsPerDay) + secondsOfDay(civil);
}

} // namespace

std::uint64_t toFiletime(DosTimestamp stamp) noexcept {
	const auto civil = unpack(stamp);
	if (!isExpressible(civil)) {
		return 0;
	}
	const auto seconds = unixSecondsOf(civil) + kUnixEpochInFiletimeSeconds;
	return static_cast<std::uint64_t>(seconds) * kTicksPerSecond;
}

} // namespace revenant::fs
