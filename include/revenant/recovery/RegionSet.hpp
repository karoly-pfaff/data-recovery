// SPDX-License-Identifier: GPL-3.0-or-later
// "Which bytes of the device are already spoken for" — the primitive under
// both byte accounting (what the names claim) and arbitration (what the
// winners claim).
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::recovery {

// A set of device byte ranges, kept fused and in offset order so it stays
// proportional to distinct regions rather than to how many were added.
class RegionSet {
public:
	// Claims `region`, fusing it with anything it overlaps or touches.
	void add(const fs::Extent& region);

	// True when any byte of `region` is already claimed. Touching end to end
	// is not overlapping: a file may begin exactly where another ended.
	[[nodiscard]] bool overlaps(const fs::Extent& region) const;

	// The complement within `[0, deviceSize)`, in offset order, never
	// zero-length.
	[[nodiscard]] std::vector<carve::ScanRegion> gaps(std::uint64_t deviceSize) const;

	[[nodiscard]] std::uint64_t claimedBytes() const noexcept;

	[[nodiscard]] std::size_t regionCount() const noexcept;

private:
	// Start offset -> one-past-the-end. An ordered map rather than a vector so
	// a claim costs a lookup and a local fuse, not a re-sort: arbitration adds
	// in confidence order, which is nothing like device order.
	std::map<std::uint64_t, std::uint64_t> regions_;
};

} // namespace revenant::recovery
