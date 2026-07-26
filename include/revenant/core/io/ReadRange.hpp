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
// closeNative/readNative/acquireImage below are declared here but
// implemented per-platform (ImageFileDevicePosix.cpp /
// ImageFileDeviceWindows.cpp); the three ImageFileDevice member functions
// that call them (the destructor, readAt, open) are defined once in the
// unconditionally-compiled src/core/io/ImageFileDeviceShared.cpp — not
// here — precisely so this header stays free of non-inline definitions.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <utility>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant {

// Implemented per-platform. closeNative releases the native handle
// (destructor); readNative performs a positioned, already-clamped read
// against it; acquireImage opens an image and determines its size,
// returning the native handle already normalized to std::intptr_t (see
// openWithSize below).
void closeNative(std::intptr_t nativeHandle) noexcept;
Result<std::size_t>
readNative(std::intptr_t nativeHandle, std::uint64_t offset, std::span<std::byte> buffer);
Result<std::pair<std::intptr_t, std::uint64_t>>
acquireImage(const std::filesystem::path& imagePath);

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

// Opens a native platform resource (already attempted, e.g.
// `openFd(path)`/`openHandle(path)`), queries its size, and normalizes the
// resource to std::intptr_t. `queryFileSize` looks up the size given the raw
// native value, closing it itself on failure (ownership never transferred
// out); `toIntPtr` performs the platform's native-handle -> std::intptr_t
// conversion (`static_cast` on POSIX, `std::bit_cast` on Windows).
template <typename Native, typename QuerySize, typename ToIntPtr>
Result<std::pair<std::intptr_t, std::uint64_t>>
openWithSize(Result<Native> native, QuerySize&& queryFileSize, ToIntPtr&& toIntPtr) {
	if (!native.hasValue()) {
		return native.error();
	}
	const auto size = std::forward<QuerySize>(queryFileSize)(native.value());
	if (!size.hasValue()) {
		return size.error();
	}
	return std::pair{std::forward<ToIntPtr>(toIntPtr)(native.value()), size.value()};
}

} // namespace revenant
