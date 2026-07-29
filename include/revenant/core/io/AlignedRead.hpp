// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Platform-neutral logic shared by RawDeviceWindows.cpp and RawDevicePosix.cpp:
// turning a caller's arbitrary read into the sector-aligned one a raw device
// will actually accept, and slicing the answer back down to what was asked for.
// Pure logic only — no OS headers, no #ifdef — so it compiles and clang-tidies
// identically on every platform, and every helper here is inline or a template,
// so this header is safely includable from any translation unit (including unit
// tests) without ODR risk.
//
// `BlockDevice` promises that reads need not be aligned, and every parser in the
// tree takes that promise: an MFT record is 1024 bytes at an arbitrary offset, a
// directory entry is 32. A raw device refuses both. The reconciliation happens
// here, once, rather than in four filesystem parsers.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"

namespace revenant {

// What a caller asked for, named so the two numbers cannot be swapped.
struct ByteRange {
	std::uint64_t offset = 0;
	std::size_t length = 0;
};

// The aligned read that covers a `ByteRange`: an offset rounded down and a
// length rounded up to whole sectors, plus how far into it the caller's own
// bytes begin.
struct AlignedWindow {
	std::uint64_t offset = 0;
	std::size_t length = 0;
	std::size_t skip = 0;
};

// A sector size of zero would divide by zero and cannot describe any device, so
// it is read as one — which makes every range already aligned and costs nothing.
[[nodiscard]] inline std::uint64_t sectorsOf(std::uint32_t sectorSize) {
	return sectorSize == 0 ? 1 : sectorSize;
}

[[nodiscard]] inline AlignedWindow
alignedWindow(const ByteRange& wanted, std::uint32_t sectorSize) {
	const std::uint64_t sector = sectorsOf(sectorSize);
	const std::uint64_t start = (wanted.offset / sector) * sector;
	const std::uint64_t skip = wanted.offset - start;
	const std::uint64_t span = skip + wanted.length;
	return AlignedWindow{
		.offset = start,
		.length = static_cast<std::size_t>(((span + sector - 1) / sector) * sector),
		.skip = static_cast<std::size_t>(skip)};
}

// How much of an aligned read the caller actually asked for. The window starts
// *before* the caller's offset, so a read that stopped short has to be measured
// from `skip` — a read that stopped before the caller's range even began
// supplies nothing, and counting it from zero would hand back bounce-buffer
// bytes nobody wrote.
[[nodiscard]] inline std::size_t
usableBytes(const AlignedWindow& window, std::size_t read, std::size_t wanted) {
	if (read <= window.skip) {
		return 0;
	}
	return std::min(wanted, read - window.skip);
}

// Satisfies an arbitrary read out of a device that answers only aligned ones.
// `readAligned(offset, buffer)` is the platform's own read; it is only ever
// called with an offset and a length that are whole multiples of `sectorSize`.
//
// A request that is already aligned is handed straight to `readAligned` with the
// caller's own buffer, so the common case — a cache or a scan window above this
// — costs no copy at all.
template <typename ReadAligned>
Result<std::size_t> readThroughAlignment(
	std::uint64_t offset,
	std::span<std::byte> buffer,
	std::uint32_t sectorSize,
	const ReadAligned& readAligned) {
	const AlignedWindow window =
		alignedWindow(ByteRange{.offset = offset, .length = buffer.size()}, sectorSize);
	if (window.skip == 0 && window.length == buffer.size()) {
		return readAligned(offset, buffer);
	}
	std::vector<std::byte> bounce(window.length);
	return readAligned(window.offset, std::span{bounce}).map([&](std::size_t read) {
		const auto usable = usableBytes(window, read, buffer.size());
		std::copy_n(
			bounce.begin() + static_cast<std::ptrdiff_t>(window.skip),
			usable,
			buffer.begin());
		return usable;
	});
}

} // namespace revenant
