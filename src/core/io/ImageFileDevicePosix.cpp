// SPDX-License-Identifier: GPL-3.0-or-later
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <utility>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"

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

// Stops the loop on either an I/O error (propagated) or EOF (`total`
// returned unchanged); otherwise reports the new running total.
Result<std::size_t> readFully(int fd, std::uint64_t offset, std::span<std::byte> buffer) {
    std::size_t total = 0;
    while (total < buffer.size()) {
        const auto advanced = advanceByOneChunk(fd, offset, buffer, total);
        if (!advanced.hasValue() || advanced.value() == total) {
            return advanced.hasValue() ? Result<std::size_t>(total) : advanced;
        }
        total = advanced.value();
    }
    return total;
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

// Opens the image and determines its size as one unit; the caller only
// needs to know whether acquiring a usable, sized descriptor succeeded.
Result<std::pair<int, std::uint64_t>> openImage(const std::filesystem::path& imagePath) {
    const auto fd = openFd(imagePath);
    if (!fd.hasValue()) {
        return fd.error();
    }
    const auto size = queryFileSize(fd.value());
    if (!size.hasValue()) {
        return size.error();
    }
    return std::pair{fd.value(), size.value()};
}

} // namespace

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - matches the header's
// verbatim signature; the ConstructTag guards this constructor to open()'s
// single call site, so the swap risk this check targets does not apply.
ImageFileDevice::ImageFileDevice(ConstructTag /*unused*/,
                                 std::intptr_t nativeHandle,
                                 std::uint64_t sizeBytes,
                                 std::uint32_t sectorSize) noexcept
    : nativeHandle_(nativeHandle), sizeInBytes_(sizeBytes), sectorSize_(sectorSize) {}

// NOLINTEND(bugprone-easily-swappable-parameters)

ImageFileDevice::~ImageFileDevice() {
    ::close(static_cast<int>(nativeHandle_));
}

Result<std::unique_ptr<ImageFileDevice>>
ImageFileDevice::open(const std::filesystem::path& imagePath, std::uint32_t sectorSize) {
    if (sectorSize == 0) {
        return Error{.code = ErrorCode::kInvalidArgument};
    }
    const auto opened = openImage(imagePath);
    if (!opened.hasValue()) {
        return opened.error();
    }
    return std::make_unique<ImageFileDevice>(ConstructTag{},
                                             opened.value().first,
                                             opened.value().second,
                                             sectorSize);
}

Result<std::size_t> ImageFileDevice::readAt(std::uint64_t offset, std::span<std::byte> buffer) {
    if (buffer.size() > std::numeric_limits<std::uint64_t>::max() - offset) {
        return Error{.code = ErrorCode::kOverflow, .offset = offset};
    }
    if (offset >= sizeInBytes_ || buffer.empty()) {
        return std::size_t{0};
    }
    const auto want =
        static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), sizeInBytes_ - offset));
    return readFully(static_cast<int>(nativeHandle_), offset, buffer.first(want));
}

} // namespace revenant
