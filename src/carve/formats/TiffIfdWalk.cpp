// SPDX-License-Identifier: GPL-3.0-or-later
#include "TiffIfdWalk.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "TiffEntry.hpp"
#include "revenant/core/BoundedCount.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

namespace {

constexpr std::uint64_t kFirstIfdOffsetAt = 4;
constexpr std::size_t kEntryBytes = 12;
constexpr std::uint64_t kEntryCountBytes = 2;
constexpr std::uint64_t kNextPointerBytes = 4;
// A `next` pointer may point backwards, so the chain is capped rather than
// trusted to terminate (ADR-0009). Real RAW files use a handful of IFDs.
constexpr std::size_t kMaxIfds = 32;
// One strip per scanline of a very large image is still far below this.
constexpr std::size_t kMaxStrips = 1U << 16U;

// The two tag pairs that locate image data: strips, or tiles.
struct ImageDataTags {
	TiffEntry offsets;
	TiffEntry counts;
};

// Every IFD entry's contribution, gathered as the table is walked.
struct IfdScan {
	// Just past this table's own next-IFD pointer. Kept apart from `end`:
	// that one grows to cover values living elsewhere in the file, and using
	// it to locate the next pointer reads from wherever the last value landed.
	std::uint64_t tableEnd = 0;
	std::uint64_t end = 0;
	ImageDataTags strips;
	ImageDataTags tiles;
	TiffEntry make;
};

void recordStripTags(IfdScan& scan, const TiffEntry& entry) {
	if (entry.tag == kStripOffsetsTag) {
		scan.strips.offsets = entry;
	}
	if (entry.tag == kStripByteCountsTag) {
		scan.strips.counts = entry;
	}
}

void recordTileTags(IfdScan& scan, const TiffEntry& entry) {
	if (entry.tag == kTileOffsetsTag) {
		scan.tiles.offsets = entry;
	}
	if (entry.tag == kTileByteCountsTag) {
		scan.tiles.counts = entry;
	}
}

void recordTaggedEntry(IfdScan& scan, const TiffEntry& entry) {
	recordStripTags(scan, entry);
	recordTileTags(scan, entry);
	if (entry.tag == kMakeTag) {
		scan.make = entry;
	}
}

void scanEntry(IfdScan& scan, const TiffEntry& entry) {
	scan.end = std::max(scan.end, valueEnd(entry));
	recordTaggedEntry(scan, entry);
}

// The end of one strip or tile: where its data starts plus how long it is.
[[nodiscard]] Result<std::uint64_t>
pieceEnd(const TiffContext& tiff, const ImageDataTags& tags, std::uint32_t index) {
	const auto start = arrayElement(tiff, tags.offsets, index);
	if (!start.hasValue()) {
		return start.error();
	}
	return arrayElement(tiff, tags.counts, index).map([&start](std::uint32_t length) {
		return static_cast<std::uint64_t>(start.value()) + length;
	});
}

// The highest end over every strip or tile the entry pair describes. A pair
// that does not agree on its element count, or whose elements cannot be read,
// contributes nothing — the extent stays bounded by what is known.
[[nodiscard]] bool tagsUsable(const ImageDataTags& tags) {
	return tags.offsets.tag != 0 && tags.offsets.count == tags.counts.count &&
		   boundedCount(tags.offsets.count, kMaxStrips).hasValue();
}

[[nodiscard]] std::uint64_t highestPieceEnd(const TiffContext& tiff, const ImageDataTags& tags) {
	std::uint64_t end = 0;
	for (std::uint32_t index = 0; index < tags.offsets.count; ++index) {
		const auto piece = pieceEnd(tiff, tags, index);
		end = piece.hasValue() ? std::max(end, piece.value()) : end;
	}
	return end;
}

[[nodiscard]] std::uint64_t imageDataEnd(const TiffContext& tiff, const ImageDataTags& tags) {
	return tagsUsable(tags) ? highestPieceEnd(tiff, tags) : 0;
}

// Reads the entries of the table at `offset`; `count` leads so the context
// separates it from the offset it would otherwise sit next to.
[[nodiscard]] IfdScan
scanEntries(std::uint16_t count, const TiffContext& tiff, std::uint64_t offset) {
	const auto tableEnd = offset + kEntryCountBytes +
						  (static_cast<std::uint64_t>(count) * kEntryBytes) + kNextPointerBytes;
	IfdScan scan{.tableEnd = tableEnd, .end = tableEnd, .strips = {}, .tiles = {}, .make = {}};
	for (std::uint16_t index = 0; index < count; ++index) {
		scanEntry(scan, readEntry(tiff, offset + kEntryCountBytes + (index * kEntryBytes)).value());
	}
	return scan;
}

// One IFD, refused whole unless its entire entry table is present.
[[nodiscard]] Result<IfdScan> scanIfd(const TiffContext& tiff, std::uint64_t offset) {
	const auto count = readU16(tiff, offset);
	if (!count.hasValue()) {
		return count.error();
	}
	const auto tableBytes = static_cast<std::size_t>(count.value()) * kEntryBytes;
	if (!tiff.reader.bytes(offset + kEntryCountBytes, tableBytes).hasValue()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = offset};
	}
	return scanEntries(count.value(), tiff, offset);
}

// Folds one IFD's scan into the running outcome.
void absorb(TiffWalkOutcome& outcome, const TiffContext& tiff, const IfdScan& scan) {
	const auto dataEnd = std::max(imageDataEnd(tiff, scan.strips), imageDataEnd(tiff, scan.tiles));
	outcome.sawIfd = true;
	outcome.sawImageData = outcome.sawImageData || dataEnd > 0;
	outcome.end = std::max({outcome.end, scan.end, dataEnd});
	outcome.make = outcome.make.tag == 0 ? scan.make : outcome.make;
}

// The offset of the IFD following this table, or 0 to end the chain.
[[nodiscard]] std::uint32_t nextIfdOffset(const TiffContext& tiff, const IfdScan& scan) {
	const auto next = readU32(tiff, scan.tableEnd - kNextPointerBytes);
	return next.hasValue() ? next.value() : 0;
}

// One chain step: folds the IFD at `offset` in and reports where the next one
// starts, or an error when this table did not parse.
[[nodiscard]] Result<std::uint32_t>
stepIfd(const TiffContext& tiff, TiffWalkOutcome& outcome, std::uint64_t offset) {
	const auto scan = scanIfd(tiff, offset);
	if (!scan.hasValue()) {
		return scan.error();
	}
	absorb(outcome, tiff, scan.value());
	return nextIfdOffset(tiff, scan.value());
}

// Walks until the chain terminates, breaks, or hits the cap; returns where it
// stopped so the caller can tell a proper terminator from the other two.
[[nodiscard]] Result<std::uint32_t> runChain(const TiffContext& tiff, TiffWalkOutcome& outcome) {
	auto offset = readU32(tiff, kFirstIfdOffsetAt);
	for (std::size_t seen = 0; seen < kMaxIfds && offset.hasValue() && offset.value() != 0;
		 ++seen) {
		offset = stepIfd(tiff, outcome, offset.value());
	}
	return offset;
}

} // namespace

TiffWalkOutcome walkTiffIfds(const TiffContext& tiff) {
	TiffWalkOutcome outcome;
	const auto stopped = runChain(tiff, outcome);
	outcome.chainComplete = stopped.hasValue() && stopped.value() == 0;
	outcome.withinBounds = outcome.end <= tiff.reader.size();
	return outcome;
}

} // namespace revenant::carve
