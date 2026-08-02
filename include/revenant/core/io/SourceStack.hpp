// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <span>

#include "revenant/core/io/BadRange.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/core/io/RetryingDevice.hpp"

namespace revenant {

// The composed device a run reads its source through, and the damage that
// composition met on the way.
//
// The decorators borrow what they wrap, and `openSource` hands its result back
// by value; a stack holding one by value could not be returned without leaving
// that reference pointing at a destroyed device. Holding each by `unique_ptr`
// means moving the stack moves two pointers and relocates nothing, so every
// reference inside stays aimed at the same object.
//
// It also owns the answer no single layer can give. A `RetryingDevice` knows
// what it invented; a `PartitionView` over one does not, and would report a
// clean device while sitting on top of damage. Rather than widen `BlockDevice`
// with a question only one implementation can answer (ADR-0007), the map belongs
// to the thing that did the composing.
class SourceStack {
public:
	// `device` wrapped for a real run: a `RetryingDevice`, and nothing else.
	//
	// `CachingDevice` is deliberately *not* in this stack. story-0604 first put
	// it here, on the reasoning that the block it keeps spares a dying drive the
	// repeat reads; the benchmark gate then measured what that costs a healthy
	// one. On the carve-validate case the composed cache was 42% slower than the
	// bare device (3,070 -> 1,767 candidates/s) and CI counted fifty times the
	// instructions, because a scan reads forward in large strides and a 64 KiB
	// block cache turns each of those into an allocation and a second copy. Its
	// other claim — that it makes every read sector-aligned for a Windows raw
	// device — is redundant: `RawDevice::readAt` aligns its own reads.
	//
	// What would put it back is a measurement on the access pattern it was built
	// for, on a device where read *latency* dominates rather than an image file
	// on local storage. Until someone has that number, no run pays for it.
	//
	// `policy` is what a run should try before giving up on a sector, and a
	// production run takes the default: that is a property of failing hardware,
	// not of the source it was pointed at. It is a parameter because the pause
	// is real time — reaching `kLostSourceRunBytes` at the default costs
	// minutes — and a test that waited for it would measure the clock.
	[[nodiscard]] static SourceStack
	over(std::unique_ptr<BlockDevice> device, const RetryPolicy& policy = RetryPolicy{});

	// What everything above the I/O layer reads through.
	[[nodiscard]] BlockDevice& top() noexcept;

	// Every range this run was handed as zeros because the device would not give
	// them up: device-absolute, and a set in offset order rather than a log of
	// the reads that met them — see `RetryingDevice::badRanges`. Empty for a
	// source that answered everything asked of it.
	//
	// The span is valid until the next read through `top()`, which may append to
	// the map and reallocate it. Ask for it where the reading has stopped —
	// which is also the only place the answer is complete.
	[[nodiscard]] std::span<const BadRange> badRanges() const noexcept;

private:
	SourceStack(
		std::unique_ptr<BlockDevice> device,
		std::unique_ptr<RetryingDevice> retrying) noexcept;

	// Declared bottom-up, which is the order they must be destroyed in reverse
	// of: the retry layer borrows the device below it.
	std::unique_ptr<BlockDevice> device_;
	std::unique_ptr<RetryingDevice> retrying_;
};

} // namespace revenant
