// SPDX-License-Identifier: GPL-3.0-or-later
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <utility>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/ReadRange.hpp"

namespace revenant {

namespace {

// One pread(2) attempt at `position`, retried internally on EINTR; a
// negative result leaves the (non-EINTR) errno for the caller to report.
::ssize_t preadRetryingEintr(int fd, std::uint64_t position, std::span<std::byte> chunk) {
	for (;;) {
		const ::ssize_t got =
			::pread(fd, chunk.data(), chunk.size(), static_cast<::off_t>(position));
		if (got >= 0 || errno != EINTR) {
			return got;
		}
	}
}

// Reads one chunk into `buffer[total:]` and folds the result into `total`.
// A same-as-`total` result means end-of-file (nothing left to accumulate).
Result<std::size_t>
advanceByOneChunk(int fd, std::uint64_t offset, std::span<std::byte> buffer, std::size_t total) {
	const ::ssize_t got = preadRetryingEintr(fd, offset + total, buffer.subspan(total));
	if (got < 0) {
		return Error{.code = ErrorCode::kIoFailure, .offset = offset + total, .osCode = errno};
	}
	return total + static_cast<std::size_t>(got);
}

Result<std::size_t> readFully(int fd, std::uint64_t offset, std::span<std::byte> buffer) {
	return driveReadLoop(buffer.size(), [&](std::size_t total) {
		return advanceByOneChunk(fd, offset, buffer, total);
	});
}

// Opens the raw file descriptor; missing/unreadable path -> kNotFound.
Result<int> openFd(const std::filesystem::path& imagePath) {
	// open(2) is variadic only for the optional `mode` argument (unused
	// here); POSIX offers no non-vararg alternative for opening by path.
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
	const int fd = ::open(imagePath.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return Error{.code = ErrorCode::kNotFound, .offset = 0, .osCode = errno};
	}
	return fd;
}

// Queries the file size; closes `fd` itself on failure since the caller
// never took ownership in that case.
Result<std::uint64_t> queryFileSize(int fd) {
	struct ::stat info{};
	if (::fstat(fd, &info) != 0) {
		const int savedErrno = errno;
		::close(fd);
		return Error{.code = ErrorCode::kIoFailure, .offset = 0, .osCode = savedErrno};
	}
	return static_cast<std::uint64_t>(info.st_size);
}

std::intptr_t toIntPtr(int fd) {
	return static_cast<std::intptr_t>(fd);
}

} // namespace

// close(2)'s return value is intentionally ignored: the descriptor is
// released whether or not the call reports failure, so retrying (the fix
// for pread's EINTR above) would operate on an already-freed descriptor
// number. This device is read-only, so there is nothing buffered left to
// flush or report a failure for.
void closeNative(std::intptr_t nativeHandle) noexcept {
	::close(static_cast<int>(nativeHandle));
}

Result<std::size_t>
readNative(std::intptr_t nativeHandle, std::uint64_t offset, std::span<std::byte> buffer) {
	return readFully(static_cast<int>(nativeHandle), offset, buffer);
}

Result<std::pair<std::intptr_t, std::uint64_t>>
acquireImage(const std::filesystem::path& imagePath) {
	return openWithSize(openFd(imagePath), queryFileSize, toIntPtr);
}

} // namespace revenant
