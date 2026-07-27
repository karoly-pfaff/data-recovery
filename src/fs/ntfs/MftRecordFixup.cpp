// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "MftRecordInternal.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ntfs {

namespace {

constexpr std::size_t kStrideSize = 512;

struct UsaHeader {
	std::uint16_t offset{};
	std::uint16_t count{};
};

[[nodiscard]] Result<bool>
validateUsaBounds(std::uint16_t usaOffset, std::uint16_t usaCount, std::size_t recordSize) {
	if (usaCount == 0) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = 0x06};
	}
	if (static_cast<std::uint64_t>(usaOffset) + (static_cast<std::uint64_t>(usaCount) * 2) >
		recordSize) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = 0x04};
	}
	if (usaCount > 1 && (static_cast<std::uint64_t>(usaCount) - 1) * kStrideSize > recordSize) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = 0x06};
	}
	return {true};
}

[[nodiscard]] Result<UsaHeader> readUsaHeader(std::span<const std::byte> record) {
	const ByteReader reader{record};
	const auto h = UsaHeader{
		.offset = reader.readLe<std::uint16_t>(0x04).value(),
		.count = reader.readLe<std::uint16_t>(0x06).value()};
	const auto bounds = validateUsaBounds(h.offset, h.count, record.size());
	if (!bounds.hasValue()) {
		return bounds.error();
	}
	return h;
}

[[nodiscard]] Result<std::uint16_t>
readUsnValue(const std::vector<std::byte>& fixedUp, std::uint16_t usaOffset) {
	const ByteReader reader{fixedUp};
	return reader.readLe<std::uint16_t>(usaOffset).value();
}

void writeSavedWord(std::vector<std::byte>& fixedUp, std::size_t tailOffset, std::uint16_t saved) {
	// Bounds verified by validateUsaBounds: tailOffset and tailOffset + 1 are within fixedUp.
	// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
	fixedUp[tailOffset] = std::byte{static_cast<unsigned char>(saved & 0xFFU)};
	// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
	fixedUp[tailOffset + 1] = std::byte{static_cast<unsigned char>(saved >> 8)};
}

// USA offset and USN value are semantically distinct; the index i separates the two
// std::uint16_t parameters.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
[[nodiscard]] bool applyOneFixup(
	std::vector<std::byte>& fixedUp,
	std::uint16_t usaOffset,
	std::size_t i,
	std::uint16_t usn) {
	const ByteReader reader{fixedUp};
	const auto saved = reader.readLe<std::uint16_t>(usaOffset + 2 + (i * 2)).value();
	const auto tailOffset = ((i + 1) * kStrideSize) - 2;
	const auto tail = reader.readLe<std::uint16_t>(tailOffset).value();
	if (tail != usn) {
		return false;
	}
	writeSavedWord(fixedUp, tailOffset, saved);
	return true;
}

// NOLINTEND(bugprone-easily-swappable-parameters)

[[nodiscard]] bool applyAllFixups(FixupOutcome& outcome, const UsaHeader& h, std::uint16_t usn) {
	for (std::size_t i = 0; i < static_cast<std::size_t>(h.count) - 1; ++i) {
		if (!applyOneFixup(outcome.fixedUp, h.offset, i, usn)) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] Result<FixupOutcome> applyFixupStrides(FixupOutcome outcome, const UsaHeader& h) {
	const auto usn = readUsnValue(outcome.fixedUp, h.offset);
	if (!usn.hasValue()) {
		return usn.error();
	}
	if (!applyAllFixups(outcome, h, usn.value())) {
		return outcome;
	}
	return FixupOutcome{.applied = true, .fixedUp = std::move(outcome.fixedUp)};
}

} // namespace

Result<FixupOutcome> applyUpdateSequenceFixup(std::span<const std::byte> raw) {
	FixupOutcome outcome{
		.applied = false,
		.fixedUp = std::vector<std::byte>{raw.begin(), raw.end()}};
	const auto usa = readUsaHeader(outcome.fixedUp);
	if (!usa.hasValue()) {
		return usa.error();
	}
	if (usa.value().count == 1) {
		return FixupOutcome{.applied = true, .fixedUp = std::move(outcome.fixedUp)};
	}
	return applyFixupStrides(std::move(outcome), usa.value());
}

} // namespace revenant::fs::ntfs
