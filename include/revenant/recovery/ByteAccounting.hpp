// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::recovery {

// ADR-0009 bounded allocation: the accounted set is sized by on-disk metadata
// (records x runs), so it may not grow unchecked. Dropping past this cap is
// safe in a way dropping a *candidate* would not be — less accounting only
// ever means more scanning, never less.
inline constexpr std::size_t kMaxAccountedRegions = std::size_t{1} << 16U;

// The device regions confidently recovered files already account for, and the
// gaps between them — the only places the carve pass needs to look.
//
// This is a performance optimization, not the correctness authority: what
// stops a carve candidate from being emitted over a named file's bytes is
// arbitration (ADR-0006), not this set.
class ByteAccounting {
public:
	// Takes the extents of an entry whose metadata is trustworthy enough to
	// speak for those bytes. An `kUncertain` entry contributes nothing: hybrid
	// orchestration calls that region a safety net, so it stays scannable.
	void account(const fs::RecoveredEntry& entry);

	// The complement of everything accounted for, within `[0, deviceSize)`, in
	// offset order and never empty-length. Regions that overlap or touch are
	// fused first, so the result is proportional to distinct regions rather
	// than to file count.
	[[nodiscard]] std::vector<carve::ScanRegion> gaps(std::uint64_t deviceSize) const;

	// Bytes covered once fusing has removed the overlaps.
	[[nodiscard]] std::uint64_t accountedBytes() const;

	// Extents refused because the set was full — reported rather than hidden.
	[[nodiscard]] std::uint64_t droppedRegions() const noexcept;

private:
	void addRegion(const fs::Extent& extent);

	// The accounted set, fused and in offset order.
	[[nodiscard]] std::vector<fs::Extent> fusedRegions() const;

	std::vector<fs::Extent> accounted_;
	std::uint64_t dropped_ = 0;
};

} // namespace revenant::recovery
