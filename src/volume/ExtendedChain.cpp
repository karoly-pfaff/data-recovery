// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/volume/Mbr.hpp"
#include "revenant/volume/MbrPartitions.hpp"
#include "volume/MbrInternal.hpp"
#include "volume/SectorIo.hpp"

namespace revenant::volume {

namespace {

// An EBR uses only its first two slots. The first describes the one logical
// partition this link holds; the second, when it names a container, points at
// the next link.
constexpr std::size_t kLogicalSlot = 0;
constexpr std::size_t kNextEbrSlot = 1;

// What one EBR contributed to the walk.
struct ChainLink {
	MbrPartition partition{};
	bool describesPartition = false;
	std::uint64_t nextEbrLba = 0;
};

// The walk over one extended partition's chain. It holds where the walk is and
// what it has found; the device it reads through is a collaborator handed to
// each step, not state of its own.
//
// Every LBA here stays far inside 64 bits without a check: the base it resolves
// against is the 32-bit field a table slot can state, so no sum of two of them
// can wrap.
class ChainWalk {
public:
	explicit ChainWalk(std::uint32_t extendedStartLba) noexcept
		: extendedStartLba_(extendedStartLba), ebrLba_(extendedStartLba) {}

	[[nodiscard]] std::vector<MbrPartition> collect(BlockDevice& device) {
		while (canStep() && step(device)) {}
		return std::move(found_);
	}

private:
	// Both bounds are needed. A revisit check alone lets a crafted table spell a
	// chain billions of links long; a length cap alone lets a two-link cycle spin
	// until it reaches the cap.
	[[nodiscard]] bool canStep() const {
		return ebrLba_ != 0 && found_.size() < kMaxLogicalPartitions &&
			   std::ranges::find(seen_, ebrLba_) == seen_.end();
	}

	// One link: remember where the walk is, read it, fold it in. A false return
	// ends the chain.
	[[nodiscard]] bool step(BlockDevice& device) {
		seen_.push_back(ebrLba_);
		const auto link = readLink(device);
		if (!link.hasValue()) {
			return false;
		}
		record(link.value());
		ebrLba_ = link.value().nextEbrLba;
		return true;
	}

	void record(const ChainLink& link) {
		if (link.describesPartition) {
			found_.push_back(link.partition);
		}
	}

	[[nodiscard]] Result<ChainLink> readLink(BlockDevice& device) const {
		return readTableSector(device, ebrLba_)
			.andThen([](const TableSector& sector) { return parseMbrSector(sector); })
			.andThen([this, &device](const MbrTable& table) { return linkOf(device, table); });
	}

	[[nodiscard]] Result<ChainLink> linkOf(const BlockDevice& device, const MbrTable& table) const {
		const auto next = nextLbaOf(table.entries.at(kNextEbrSlot));
		return logicalOf(device, table.entries.at(kLogicalSlot)).map([next](ChainLink held) {
			held.nextEbrLba = next;
			return held;
		});
	}

	// Slot 0's start is relative to the EBR holding it — the address this walk is
	// standing on. A slot that is unused, or that names another container, holds
	// no partition of its own.
	[[nodiscard]] Result<ChainLink>
	logicalOf(const BlockDevice& device, const MbrEntry& entry) const {
		if (entry.type == kUnusedPartitionType || isExtendedType(entry.type)) {
			return ChainLink{};
		}
		const PlacedEntry resolved{
			.startLba = ebrLba_ + entry.startLba,
			.sectorCount = entry.sectorCount,
			.typeCode = entry.type,
			.logical = true};
		return partitionOf(resolved, device.sectorSize()).map([](const MbrPartition& partition) {
			return ChainLink{.partition = partition, .describesPartition = true, .nextEbrLba = 0};
		});
	}

	// Slot 1's start is relative to the *head of the extended partition*, the
	// same base for every link — not to the EBR that states it. Using one base
	// for both slots yields a chain that works on a disk with a single logical
	// partition and misplaces every one after it.
	[[nodiscard]] std::uint64_t nextLbaOf(const MbrEntry& entry) const {
		if (!isExtendedType(entry.type)) {
			return 0;
		}
		return std::uint64_t{extendedStartLba_} + entry.startLba;
	}

	std::uint32_t extendedStartLba_;
	std::uint64_t ebrLba_;
	std::vector<MbrPartition> found_;
	std::vector<std::uint64_t> seen_;
};

} // namespace

std::vector<MbrPartition> logicalPartitions(BlockDevice& device, std::uint32_t extendedStartLba) {
	ChainWalk walk{extendedStartLba};
	return walk.collect(device);
}

} // namespace revenant::volume
