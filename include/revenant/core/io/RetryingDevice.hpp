// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BadRange.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant {

// How hard a failing read is tried before the range is given up on.
struct RetryPolicy {
	unsigned attempts = 3;
	// A dying drive's own error recovery needs time, so the default is a real
	// wait. A test needs none and passes zero — which is why this is a duration
	// rather than a clock seam: a value that can be zero is a simpler thing to
	// test against, and it is what an operator would want to tune anyway.
	std::chrono::milliseconds pause{100};
};

// How much *contiguous* unreadable source a run tolerates before it stops
// calling the device damaged and starts calling it gone.
//
// The two look identical one sector at a time — both refuse after every retry —
// and they need opposite answers: a bad patch must be zero-filled and stepped
// over, a vanished device must end the run. Only their extent separates them. A
// megabyte is far more than any single media defect — a disk's reallocation runs
// to a handful of sectors, and even a flash erase block stops short of it — while
// a device that has gone refuses from the first sector onward and reaches the
// bound inside one scan chunk.
//
// Reaching it is not fast: at the default policy every sector costs three
// attempts and two pauses. That is the price of not calling a large defect a
// dead disk, and it is still bounded, which transcribing the rest of a terabyte
// as zeros is not.
inline constexpr std::uint64_t kLostSourceRunBytes = std::uint64_t{1} << 20U;

// Survives a device that will not answer. A failing read is retried whole — a
// drive that fails a large request usually reads most of it on a second attempt
// — and then, if it still fails, one sector at a time, because a hard fault
// covers a few sectors rather than the request that happened to span them.
//
// Sectors that remain unreadable are handed back as zeros and recorded.
// Abandoning the read instead would cost every file that merely *touches* the
// bad sector, so the bytes are returned and the fact that they were invented is
// kept in `badRanges()`.
//
// Until the damage stops being damage: `kLostSourceRunBytes` of contiguous
// unreadable source comes back as `kSourceLost` instead, because a device that
// has gone away would otherwise be transcribed as zeros all the way to its end.
class RetryingDevice final : public BlockDevice {
public:
	RetryingDevice(BlockDevice& source, const RetryPolicy& policy) noexcept;

	[[nodiscard]] std::uint64_t sizeInBytes() const override;
	[[nodiscard]] std::uint32_t sectorSize() const override;
	[[nodiscard]] Result<std::size_t>
	readAt(std::uint64_t offset, std::span<std::byte> buffer) override;

	// Every range this device gave back as zeros: a set, in offset order, with
	// touching and overlapping ranges folded together.
	//
	// A set and not a log, for two reasons. A long bad run must not become
	// thousands of one-sector records (ADR-0009); and a run reads the same
	// sector more than once — once to scan it, once to extract from it — so
	// recording each encounter would report twice the damage there is.
	[[nodiscard]] std::span<const BadRange> badRanges() const noexcept;

private:
	[[nodiscard]] Result<std::size_t>
	attemptRead(std::uint64_t offset, std::span<std::byte> buffer);
	[[nodiscard]] Result<std::size_t>
	readSectorwise(std::uint64_t offset, std::span<std::byte> buffer);
	[[nodiscard]] std::size_t readOneSector(std::uint64_t offset, std::span<std::byte> buffer);
	void recordBad(const BadRange& range);
	// The map folded back into a set after an insert: touching and overlapping
	// ranges become one.
	void coalesce();
	void waitBetweenAttempts() const;

	BlockDevice& source_;
	RetryPolicy policy_;
	std::vector<BadRange> bad_;
	// How much unreadable source has been handed back as zeros with no good
	// sector between. Any sector that reads resets it.
	std::uint64_t contiguousLost_ = 0;
};

} // namespace revenant
