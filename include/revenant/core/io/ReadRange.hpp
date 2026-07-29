// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Platform-neutral logic shared by ImageFileDevicePosix.cpp and
// ImageFileDeviceWindows.cpp: clamping a read range to a device's bounds,
// and driving the retry-until-full-or-EOF read loop. Pure logic only — no
// OS headers, no #ifdef — so it compiles and clang-tidies identically on
// every platform, and every helper here is either `inline` or a template,
// so this header is safely includable from any translation unit (including
// unit tests) without ODR risk.
//
// The four functions below are declared here but implemented per-platform
// (NativeIoPosix.cpp / NativeIoWindows.cpp). The shared code that calls them
// lives in unconditionally-compiled translation units — NativeSource.cpp and
// each device's *Shared.cpp — not here, precisely so this header stays free of
// non-inline definitions.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant {

// Implemented per-platform, and shared by every device backed by an OS handle.
// openReadOnly opens a path for reading and normalizes the handle to
// std::intptr_t; closeNative releases it; readNative performs a positioned,
// already-clamped read against it; queryFileSize measures an open *file*, and
// closes the handle itself when it cannot, since the caller never took ownership
// in that case.
Result<std::intptr_t> openReadOnly(const std::filesystem::path& path);
void closeNative(std::intptr_t nativeHandle) noexcept;
Result<std::size_t>
readNative(std::intptr_t nativeHandle, std::uint64_t offset, std::span<std::byte> buffer);
Result<std::uint64_t> queryFileSize(std::intptr_t nativeHandle);

// Computes how many bytes of a `bufferSize`-byte request at `offset` a
// `deviceSize`-byte device may actually service: a typed kOverflow error, or
// a clamped count. 0 covers both "the buffer is empty" and "offset is at or
// past the end" — the caller's read loop already returns 0 for a 0-length
// request, so no separate short-circuit signal is needed.
inline Result<std::size_t>
clampReadRange(std::uint64_t offset, std::size_t bufferSize, std::uint64_t deviceSize) {
	if (bufferSize > std::numeric_limits<std::uint64_t>::max() - offset) {
		return Error{.code = ErrorCode::kOverflow, .offset = offset};
	}
	if (offset >= deviceSize || bufferSize == 0) {
		return std::size_t{0};
	}
	return static_cast<std::size_t>(std::min<std::uint64_t>(bufferSize, deviceSize - offset));
}

// Drives `advance` (one platform read attempt, folding its result into the
// running total) until `bufferSize` bytes are read, an error occurs, or EOF
// is reached (a same-as-running-total result from `advance`).
// `advance` is invoked repeatedly, so it is taken by const reference — a
// forwarding reference would either be forwarded inside the loop
// (use-after-move) or never forwarded at all (missing-std-forward).
template <typename Advance>
Result<std::size_t> driveReadLoop(std::size_t bufferSize, const Advance& advance) {
	std::size_t total = 0;
	while (total < bufferSize) {
		const auto advanced = advance(total);
		if (!advanced.hasValue() || advanced.value() == total) {
			return advanced.hasValue() ? Result<std::size_t>(total) : advanced;
		}
		total = advanced.value();
	}
	return total;
}

} // namespace revenant
