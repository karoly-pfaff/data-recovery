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
#include <span>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/ReadRange.hpp"

// This platform's I/O primitives, shared by every device backed by an OS handle:
// opening one read-only, closing it, and reading positionally through it. An
// image file and a raw device differ in how they are *measured*, not in any of
// these — which is why RawDeviceWindows.cpp holds only the measuring.

namespace revenant {

namespace {

constexpr std::uint64_t kLowDwordMask = 0xFFFFFFFFULL;
constexpr std::uint32_t kDwordBits = 32;
// Cap a single ReadFile at 1 GiB; DWORD-sized lengths can't express more.
constexpr std::size_t kMaxSingleRead = 1ULL << 30U;

HANDLE toHandle(std::intptr_t raw) noexcept {
	return std::bit_cast<HANDLE>(raw);
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

// The device or file is there and the operator may not read it — the one failure
// worth telling apart, because the answer to it is "run this elevated" and the
// answer to everything else is "check the path".
[[nodiscard]] ErrorCode openFailureFor(DWORD failure) {
	if (failure == ERROR_ACCESS_DENIED) {
		return ErrorCode::kPermissionDenied;
	}
	return ErrorCode::kNotFound;
}

} // namespace

// Queries the file size; closes `handle` itself on failure since the caller
// never took ownership in that case.
Result<std::uint64_t> queryFileSize(std::intptr_t nativeHandle) {
	LARGE_INTEGER size{};
	if (::GetFileSizeEx(toHandle(nativeHandle), &size) == 0) {
		const auto osCode = static_cast<std::int32_t>(::GetLastError());
		::CloseHandle(toHandle(nativeHandle));
		return Error{.code = ErrorCode::kIoFailure, .offset = 0, .osCode = osCode};
	}
	return static_cast<std::uint64_t>(size.QuadPart);
}

// FILE_SHARE_WRITE is requested so that a disk Windows itself has open — which
// is every disk with a mounted volume on it — can still be read. No write access
// is asked for, so the source can never be modified through this handle
// (ADR-0005).
Result<std::intptr_t> openReadOnly(const std::filesystem::path& path) {
	HANDLE handle = ::CreateFileW(
		path.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		const DWORD failure = ::GetLastError();
		return Error{
			.code = openFailureFor(failure),
			.offset = 0,
			.osCode = static_cast<std::int32_t>(failure)};
	}
	return std::bit_cast<std::intptr_t>(handle);
}

// CloseHandle's BOOL return is intentionally ignored: unlike pread's EINTR
// on the POSIX side, there is no interrupted-syscall retry concern here, and
// the only realistic failure (an already-invalid handle) leaves nothing for
// a read-only device to flush or report.
void closeNative(std::intptr_t nativeHandle) noexcept {
	::CloseHandle(toHandle(nativeHandle));
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - handle and offset are
// adjacent integrals, but this is a platform primitive with one caller
// (NativeSourceDevice::readHandle), which passes its own two members by name.
// A NOLINTNEXTLINE here would land on the return type, which clang-format puts
// on a line of its own — and suppress nothing.
Result<std::size_t>
readNative(std::intptr_t nativeHandle, std::uint64_t offset, std::span<std::byte> buffer) {
	HANDLE handle = toHandle(nativeHandle);
	return driveReadLoop(buffer.size(), [handle, offset, buffer](std::size_t total) {
		return advanceByOneChunk(handle, offset, buffer, total);
	});
}

// NOLINTEND(bugprone-easily-swappable-parameters)

} // namespace revenant

// NOLINTEND(misc-include-cleaner)
