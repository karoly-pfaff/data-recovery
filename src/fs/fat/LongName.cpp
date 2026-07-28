// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fs/fat/DirectoryEntryInternal.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/fat/DirectoryEntry.hpp"

namespace revenant::fs::fat {

namespace {

constexpr std::size_t kOrdinalOffset = 0x00;
constexpr std::size_t kChecksumOffset = 0x0D;
constexpr std::uint8_t kOrdinalMask = 0x3F;
constexpr std::uint8_t kLastFragmentFlag = 0x40;
constexpr std::size_t kCodeUnitBytes = 2;

// The three runs a fragment splits its 13 code units across, in name order.
// The gaps between them hold the attribute, type and checksum bytes, which is
// why a long name is not one contiguous field.
struct NameRun {
	std::size_t offset;
	std::size_t bytes;
};

constexpr std::array<NameRun, 3> kNameRuns{
	NameRun{.offset = 0x01, .bytes = 10},
	NameRun{.offset = 0x0E, .bytes = 12},
	NameRun{.offset = 0x1C, .bytes = 4}};

// Copies one run's code units, stopping at the NUL that ends a name shorter
// than the fragment. Says whether the name ended inside this run.
// The NUL code unit that ends a name shorter than the fragment holding it.
[[nodiscard]] bool endsTheName(std::span<const std::byte> unit) {
	return unit.front() == std::byte{0} && unit.back() == std::byte{0};
}

[[nodiscard]] bool appendRun(std::vector<std::byte>& name, std::span<const std::byte> run) {
	for (std::size_t at = 0; at + kCodeUnitBytes <= run.size(); at += kCodeUnitBytes) {
		const auto unit = run.subspan(at, kCodeUnitBytes);
		if (endsTheName(unit)) {
			return true;
		}
		name.insert(name.end(), unit.begin(), unit.end());
	}
	return false;
}

[[nodiscard]] std::vector<std::byte> nameBytesOf(std::span<const std::byte> slot) {
	std::vector<std::byte> name;
	for (const NameRun& run : kNameRuns) {
		if (appendRun(name, slot.subspan(run.offset, run.bytes))) {
			return name;
		}
	}
	return name;
}

} // namespace

Result<LongNameFragment> parseLongNameFragment(std::span<const std::byte> slot) {
	if (slot.size() < kDirectoryEntryBytes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = slot.size()};
	}
	const auto entry = slot.first(kDirectoryEntryBytes);
	const auto marker = std::to_integer<std::uint8_t>(entry.subspan(kOrdinalOffset, 1).front());
	if (marker != kDeletedMarker && (marker & kOrdinalMask) == 0U) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kOrdinalOffset};
	}
	return LongNameFragment{
		.ordinal = static_cast<std::uint8_t>(marker & kOrdinalMask),
		.last = (marker & kLastFragmentFlag) != 0U,
		.deleted = marker == kDeletedMarker,
		.checksum = std::to_integer<std::uint8_t>(entry.subspan(kChecksumOffset, 1).front()),
		.nameBytes = nameBytesOf(entry)};
}

} // namespace revenant::fs::fat
