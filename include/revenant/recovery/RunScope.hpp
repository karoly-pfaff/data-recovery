// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/volume/PartitionTable.hpp"
#include "revenant/volume/PartitionView.hpp"

namespace revenant::recovery {

// The partition number that is not one: zero names the source itself — a whole
// disk, or an image of a single volume. Stated once here because a frontend,
// the resolver and every test that means "all of it" all need to say it.
inline constexpr std::uint32_t kWholeSource = 0;

// Where a run works and how its filesystem pass must read it, settled once from
// one reading of the source's partition table.
//
// A frontend states a number and nothing else: `kWholeSource`, or a number
// naming an entry of that table. Everything following from that number is
// decided here, so the
// device a run reads and the layout it walks arrive together and cannot
// disagree. A table entry names a volume and a volume does not contain a
// partition table, so a run scoped to one never looks for another; asking a
// volume whether it is a disk is what turned an intact filesystem into a
// carve-only scan that reported nothing wrong.
class RunScope {
public:
	// The scope `partition` names on `source`, which must outlive it.
	//
	// `kWholeSource` over a partitioned disk is every partition its table
	// describes; over a source carrying no readable table it is that source,
	// walked as one volume. A number the table does not carry is `kNotFound`
	// rather than a silent whole-source run, and a source whose table will not
	// read refuses a scoped run rather than guessing which range was meant.
	[[nodiscard]] static Result<RunScope> resolve(BlockDevice& source, std::uint32_t partition);

	// The device this run works in: the source, or the window one of its
	// partitions occupies.
	[[nodiscard]] BlockDevice& device() noexcept;

	// Where this run's zero sits on the source: zero for a whole-source run, the
	// window's offset for a scoped one. It is what restates anything measured in
	// the run's own coordinates back onto the disk the operator handed over —
	// which is what the bad-sector map needs to line up with an artifact's
	// extents (story-0604).
	[[nodiscard]] std::uint64_t startBytes() const noexcept;

	// The partitions the filesystem pass mounts, in `device()`'s coordinates. An
	// empty layout means one volume filling the device, which is what a scoped
	// run and an unpartitioned image both are.
	[[nodiscard]] std::span<const volume::Partition> layout() const noexcept;

private:
	RunScope(
		BlockDevice& device,
		std::unique_ptr<volume::PartitionView> window,
		std::vector<volume::Partition> layout,
		std::uint64_t startBytes) noexcept;

	// A window over one of the source's partitions, owned by the scope it
	// belongs to. A whole-source scope needs no such factory: it is the private
	// constructor with no window.
	[[nodiscard]] static RunScope
	windowOnto(BlockDevice& source, const volume::Partition& partition);

	BlockDevice* device_; // non-owning, never null; `window_.get()` when there is one
	// Held by pointer, not by value: `BlockDevice` deletes copy and move, so a
	// view held by value could not be returned inside a `Result` — and this way
	// the view stays put when the scope moves, which is what keeps `device_`
	// pointing at it.
	std::unique_ptr<volume::PartitionView> window_;
	std::vector<volume::Partition> layout_;
	std::uint64_t startBytes_;
};

} // namespace revenant::recovery
