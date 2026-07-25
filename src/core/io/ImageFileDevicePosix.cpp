// SPDX-License-Identifier: GPL-3.0-or-later
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
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

// Reads one pread attempt into `buffer[total:]`, retrying internally on
// EINTR, and folds the result into `total`. A same-as-`total` result means
// end-of-file (nothing left to accumulate).
Result<std::size_t>
advanceByOneChunk(int fd, std::uint64_t offset, std::span<std::byte> buffer, std::size_t total) {
    ::ssize_t got = 0;
    do {
        got = ::pread(fd,
                      buffer.data() + total,
                      buffer.size() - total,
                      static_cast<::off_t>(offset + total));
    } while (got < 0 && errno == EINTR);
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
