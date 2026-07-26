// SPDX-License-Identifier: GPL-3.0-or-later
// NOLINTBEGIN(misc-include-cleaner) - windows.h is the single correct include
// for the Win32 API surface below (HANDLE, OVERLAPPED, DWORD, ReadFile,
// GetLastError, CreateFileW, GetFileSizeEx, CloseHandle, and their
// associated constants/typedefs). clang-tidy's IWYU mapping has no entries
// for its internal constituent headers (fileapi.h, handleapi.h, winbase.h,
// errhandlingapi.h, minwinbase.h), which are not meant to be included
// standalone, so misc-include-cleaner cannot resolve a "direct" header here.
#include <windows.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <utility>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"
#include "revenant/core/io/ReadRange.hpp"

namespace revenant {

namespace {

constexpr std::uint64_t kLowDwordMask = 0xFFFFFFFFULL;
constexpr std::uint32_t kDwordBits = 32;
// Cap a single ReadFile at 1 GiB; DWORD-sized lengths can't express more.
constexpr std::size_t kMaxSingleRead = 1ULL << 30U;

HANDLE toHandle(std::intptr_t raw) noexcept {
	return std::bit_cast<HANDLE>(raw);
}

std::intptr_t toIntPtr(HANDLE handle) {
	return std::bit_cast<std::intptr_t>(handle);
}

// OVERLAPPED's Offset/OffsetHigh live in an anonymous union mandated by the
// Win32 struct layout; there is no alternative access pattern.
OVERLAPPED overlappedAt(std::uint64_t position) noexcept {
	OVERLAPPED overlapped{};
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
	overlapped.Offset = static_cast<DWORD>(position & kLowDwordMask);
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
	overlapped.OffsetHigh = static_cast<DWORD>(position >> kDwordBits);
	return overlapped;
}

// One positioned ReadFile chunk; EOF is reported as a zero-byte read.
Result<std::size_t> readChunk(HANDLE handle, std::uint64_t position, std::span<std::byte> chunk) {
	OVERLAPPED overlapped = overlappedAt(position);
	DWORD got = 0;
	const auto want = static_cast<DWORD>(std::min(chunk.size(), kMaxSingleRead));
	if (::ReadFile(handle, chunk.data(), want, &got, &overlapped) == 0) {
		if (::GetLastError() == ERROR_HANDLE_EOF) {
			return std::size_t{0};
		}
		return Error{
			.code = ErrorCode::kIoFailure,
			.offset = position,
			.osCode = static_cast<std::int32_t>(::GetLastError())};
	}
	return static_cast<std::size_t>(got);
}

// Reads one chunk and folds it into `total`; a same-as-`total` result means
// end-of-file (nothing left to accumulate).
Result<std::size_t> advanceByOneChunk(
	HANDLE handle,
	std::uint64_t offset,
	std::span<std::byte> buffer,
	std::size_t total) {
	const auto got = readChunk(handle, offset + total, buffer.subspan(total));
	if (!got.hasValue()) {
		return got;
	}
	return total + got.value();
}

Result<std::size_t> readFully(HANDLE handle, std::uint64_t offset, std::span<std::byte> buffer) {
	return driveReadLoop(buffer.size(), [&](std::size_t total) {
		return advanceByOneChunk(handle, offset, buffer, total);
	});
}

// Opens the raw file handle; missing/unreadable path -> kNotFound.
Result<HANDLE> openHandle(const std::filesystem::path& imagePath) {
	HANDLE handle = ::CreateFileW(
		imagePath.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		return Error{
			.code = ErrorCode::kNotFound,
			.offset = 0,
			.osCode = static_cast<std::int32_t>(::GetLastError())};
	}
	return handle;
}

// Queries the file size; closes `handle` itself on failure since the caller
// never took ownership in that case.
Result<std::uint64_t> queryFileSize(HANDLE handle) {
	LARGE_INTEGER size{};
	if (::GetFileSizeEx(handle, &size) == 0) {
		const auto osCode = static_cast<std::int32_t>(::GetLastError());
		::CloseHandle(handle);
		return Error{.code = ErrorCode::kIoFailure, .offset = 0, .osCode = osCode};
	}
	return static_cast<std::uint64_t>(size.QuadPart);
}

} // namespace

// CloseHandle's BOOL return is intentionally ignored: unlike pread's EINTR
// on the POSIX side, there is no interrupted-syscall retry concern here, and
// the only realistic failure (an already-invalid handle) leaves nothing for
// a read-only device to flush or report.
void closeNative(std::intptr_t nativeHandle) noexcept {
	::CloseHandle(toHandle(nativeHandle));
}

Result<std::size_t>
readNative(std::intptr_t nativeHandle, std::uint64_t offset, std::span<std::byte> buffer) {
	return readFully(toHandle(nativeHandle), offset, buffer);
}

Result<std::pair<std::intptr_t, std::uint64_t>>
acquireImage(const std::filesystem::path& imagePath) {
	return openWithSize(openHandle(imagePath), queryFileSize, toIntPtr);
}

} // namespace revenant

// NOLINTEND(misc-include-cleaner)
