// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <span>

#include "revenant/core/io/BadRange.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/core/io/CachingDevice.hpp"
#include "revenant/core/io/RetryingDevice.hpp"

namespace revenant {

// The composed device a run reads its source through, and the damage that
// composition met on the way.
//
// The decorators borrow what they wrap, and `openSource` hands its result back
// by value; a stack holding them by value could not be returned without leaving
// those references pointing at a destroyed device. Holding each by `unique_ptr`
// means moving the stack moves three pointers and relocates nothing, so every
// reference inside stays aimed at the same object.
//
// It also owns the answer no single layer can give. A `RetryingDevice` knows
// what it invented; a `PartitionView` over one does not, and would report a
// clean device while sitting on top of damage. Rather than widen `BlockDevice`
// with a question only one implementation can answer (ADR-0007), the map belongs
// to the thing that did the composing.
class SourceStack {
public:
	// `device` wrapped for a real run: retry nearest the device, cache above it.
	// That order is what keeps a bad sector cheap — the retry layer narrows
	// sector by sector against the real device rather than having each attempt
	// amplified into a whole-block re-read, and the zero-filled block the cache
	// keeps spares the drive every repeat read *while that block is resident*.
	// Past the cache's capacity it is not, and a long run does meet the same
	// sector again; what does not change is `badRanges()`, which is a set.
	[[nodiscard]] static SourceStack over(std::unique_ptr<BlockDevice> device);

	// What everything above the I/O layer reads through.
	[[nodiscard]] BlockDevice& top() noexcept;

	// Every range this run was handed as zeros because the device would not give
	// them up: device-absolute, and a set in offset order rather than a log of
	// the reads that met them — see `RetryingDevice::badRanges`. Empty for a
	// source that answered everything asked of it.
	[[nodiscard]] std::span<const BadRange> badRanges() const noexcept;

private:
	SourceStack(
		std::unique_ptr<BlockDevice> device,
		std::unique_ptr<RetryingDevice> retrying,
		std::unique_ptr<CachingDevice> caching) noexcept;

	// Declared bottom-up, which is also the order they must be destroyed in
	// reverse of: each borrows the one above it in this list.
	std::unique_ptr<BlockDevice> device_;
	std::unique_ptr<RetryingDevice> retrying_;
	std::unique_ptr<CachingDevice> caching_;
};

} // namespace revenant
