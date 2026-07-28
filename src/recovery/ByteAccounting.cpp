// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/ByteAccounting.hpp"

#include <cstdint>
#include <vector>

#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/RegionSet.hpp"

namespace revenant::recovery {

void ByteAccounting::addRegion(const fs::Extent& extent) {
	// ADR-0009: the extent count comes from on-disk metadata, so it may not
	// grow the set unchecked. Past the cap the extent is dropped and counted —
	// less accounting only ever means more scanning.
	if (claimed_.regionCount() >= kMaxAccountedRegions) {
		++dropped_;
		return;
	}
	claimed_.add(extent);
}

void ByteAccounting::account(const fs::RecoveredEntry& entry) {
	if (entry.recoverability != Confidence::kValid) {
		return;
	}
	for (const fs::Extent& extent : entry.extents) {
		addRegion(extent);
	}
}

std::vector<carve::ScanRegion> ByteAccounting::gaps(std::uint64_t deviceSize) const {
	return claimed_.gaps(deviceSize);
}

std::uint64_t ByteAccounting::accountedBytes() const {
	return claimed_.claimedBytes();
}

std::uint64_t ByteAccounting::droppedRegions() const noexcept {
	return dropped_;
}

} // namespace revenant::recovery
