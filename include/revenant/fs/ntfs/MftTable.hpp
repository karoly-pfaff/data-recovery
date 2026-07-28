// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/MftRecord.hpp"

namespace revenant::fs::ntfs {

// The `$MFT` describes itself in record 0, which is why opening the table is a
// parse and not a division.
inline constexpr std::uint64_t kMftRecordNumber = 0;

// The `$MFT` as a table of records addressable by number. The MFT is itself a
// file: where its records live comes from record 0's own `$DATA` runlist, so a
// fragmented MFT reads correctly instead of being assumed contiguous.
//
// Read-only throughout (ADR-0005); the device is borrowed, never owned, and
// must outlive the table.
class MftTable {
public:
	[[nodiscard]] static Result<MftTable> open(BlockDevice& device, const NtfsGeometry& geometry);

	[[nodiscard]] std::uint64_t recordCount() const noexcept;
	[[nodiscard]] const NtfsGeometry& geometry() const noexcept;

	// Reads and parses record `number`. Failures stay typed and distinct: a
	// number past the table is kOutOfRange, an empty slot is the parser's
	// kNotFound, and a device fault is kIoFailure — the caller decides which of
	// those is fatal to a whole-table walk.
	[[nodiscard]] Result<MftRecordView> readRecord(std::uint64_t number) const;

private:
	// What opening the table established: where its records are, and how many.
	struct Layout {
		std::vector<Extent> extents;
		std::uint64_t recordCount;
	};

	[[nodiscard]] static Result<Layout>
	layoutFrom(const MftRecordView& recordZero, const NtfsGeometry& geometry);

	MftTable(BlockDevice& device, const NtfsGeometry& geometry, Layout layout);

	[[nodiscard]] Result<std::uint64_t> recordOffset(std::uint64_t number) const;

	BlockDevice* device_; // non-owning, never null
	NtfsGeometry geometry_;
	Layout layout_;
};

} // namespace revenant::fs::ntfs
