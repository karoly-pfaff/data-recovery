// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "revenant/carve/FormatCarver.hpp"

namespace revenant::carve {

// One registered signature, flattened out of the carver that owns it. The
// scanner visits these instead of walking carvers and then their signature
// lists, which is what lets one pass over a window answer for all of them.
struct SignatureEntry {
	std::span<const std::byte> magic;
	// Where the magic sits inside the file (`Signature::offset`). MP4's `ftyp`
	// is at 4, which is why a hit is not a candidate start.
	std::size_t inFileOffset = 0;
	const FormatCarver* carver = nullptr;
	// The carver's position in the registry. Two candidates starting at the
	// same byte are ordered by it, so the match order stays a contract rather
	// than an accident of how the window was walked.
	std::uint32_t carverIndex = 0;
};

// Whether this build's matcher may use a CPU fast path at all. The operator's
// escape hatch (`--force-portable`) is the reason this is a value rather than a
// question the matcher asks the CPU: if the fast path misbehaves on a
// particular machine, the person whose photographs are on the disk needs a way
// to turn it off without waiting for a release.
enum class MatchPath : std::uint8_t { kAuto, kPortableOnly };

// A conservative reject in the shape a vector unit can answer: each byte is
// split into nibbles, and a position survives when the low nibble's mask and
// the high nibble's mask share a bit.
//
// It can pass a byte no signature begins with, because a high nibble and that
// nibble plus eight share a mask bit. Passing too many positions costs nothing
// — every survivor goes to the same exact comparison that the portable path
// sends it to — while dropping one would change what a scan finds, so it never
// does. That asymmetry is what makes the fast path's output identical by
// construction rather than by hope.
struct NibbleFilter {
	std::array<std::uint8_t, 16> low{};
	std::array<std::uint8_t, 16> high{};

	// Whether `value` survives. The scalar spelling of what the vector code
	// does 32 positions at a time, and what the unit tests pin.
	[[nodiscard]] bool passes(std::byte value) const noexcept {
		const auto raw = std::to_integer<std::size_t>(value);
		return (low.at(raw & 0x0FU) & high.at(raw >> 4U)) != 0;
	}
};

// Which signatures can begin at a given byte. The common case in a device scan
// is that none can, and answering that costs one indexed load and one compare —
// which is the whole point, because that question is asked once per byte of
// every disk.
//
// Grouped by first byte rather than held as a per-byte bitmask: a mask of
// signature indices would cap the registry at as many signatures as the mask
// has bits, and the 65th carver would have to fail somewhere that has no way to
// report it.
class SignatureTable {
public:
	// Built from the whole carver list rather than appended to. The table is
	// tiny and a rebuild cannot leave it half-agreeing with the registry.
	// `path` decides whether the CPU is asked about its fast path — asked here,
	// once, so nothing queries CPUID inside the loop this exists to speed up,
	// and so no lazily initialized global becomes a data race when the scan is
	// sharded across threads (story-0504).
	void rebuild(std::span<const std::unique_ptr<FormatCarver>> carvers, MatchPath path);

	// Whether the vectorized reject will be used: this build has it, the CPU
	// supports it, and the operator did not turn it off.
	[[nodiscard]] bool usesFastPath() const noexcept;

	// The vector-shaped reject the fast path evaluates.
	[[nodiscard]] const NibbleFilter& nibbleFilter() const noexcept;

	// Whether no signature at all can begin with `first`. One indexed load and
	// one test, and it is the answer for almost every byte of almost every
	// device — which is why it is a question of its own rather than an empty
	// span handed back by the call below.
	//
	// Defined here rather than in the translation unit, because this is asked
	// once per byte of the device and the project does not link with LTO: out
	// of line it was a call per byte, and inline it is a load per byte. That
	// alone was the difference between half the throughput and all of it.
	// Sliced rather than indexed because a bounds check here is a bounds check
	// on every byte of a failing disk, and `first` is a byte, so it is always
	// inside a 256-entry table.
	[[nodiscard]] bool none(std::byte first) const noexcept {
		const auto at = std::to_integer<std::size_t>(first);
		return std::span{anyStartsWith_}.subspan(at, 1).front() == 0;
	}

	// The signatures that can begin with `first`, in registration order. Asked
	// only of the bytes `none` did not dismiss.
	[[nodiscard]] std::span<const SignatureEntry> startingWith(std::byte first) const noexcept;

	[[nodiscard]] std::size_t size() const noexcept;

private:
	static constexpr std::size_t kByteValues = 256;

	// Records where each first-byte group starts, once `entries_` is sorted.
	void indexGroups() noexcept;

	[[nodiscard]] std::size_t groupEnd(std::size_t value) const noexcept;

	void buildNibbleFilter() noexcept;

	// Entries grouped by their magic's first byte, and where each group starts.
	// One extra slot at the end so a group's end is the next group's start.
	std::vector<SignatureEntry> entries_;
	std::array<std::uint16_t, kByteValues + 1> groupBegin_{};
	// Whether each byte's group holds anything. Derived from `groupBegin_` in
	// the same pass that builds it — an index, not a second source of truth —
	// and kept because the reject is the hot path: 256 bytes stay in cache,
	// and asking `groupBegin_` instead would cost two loads and a subtraction
	// on every byte of a terabyte.
	std::array<std::uint8_t, kByteValues> anyStartsWith_{};
	NibbleFilter nibbleFilter_;
	bool usesFastPath_ = false;
};

} // namespace revenant::carve
