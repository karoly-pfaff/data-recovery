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
#include <limits>
#include <memory>
#include <span>
#include <utility>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"

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
        return Error{.code = ErrorCode::kIoFailure,
                     .offset = position,
                     .osCode = static_cast<std::int32_t>(::GetLastError())};
    }
    return static_cast<std::size_t>(got);
}

// Reads one chunk and folds it into `total`; a same-as-`total` result means
// end-of-file (nothing left to accumulate).
Result<std::size_t> advanceByOneChunk(HANDLE handle,
                                      std::uint64_t offset,
                                      std::span<std::byte> buffer,
                                      std::size_t total) {
    const auto got = readChunk(handle, offset + total, buffer.subspan(total));
    if (!got.hasValue()) {
        return got;
    }
    return total + got.value();
}

// Stops the loop on either an I/O error (propagated) or EOF (`total`
// returned unchanged); otherwise reports the new running total.
Result<std::size_t> readFully(HANDLE handle, std::uint64_t offset, std::span<std::byte> buffer) {
    std::size_t total = 0;
    while (total < buffer.size()) {
        const auto advanced = advanceByOneChunk(handle, offset, buffer, total);
        if (!advanced.hasValue() || advanced.value() == total) {
            return advanced.hasValue() ? Result<std::size_t>(total) : advanced;
        }
        total = advanced.value();
    }
    return total;
}

// Opens the raw file handle; missing/unreadable path -> kNotFound.
Result<HANDLE> openHandle(const std::filesystem::path& imagePath) {
    HANDLE handle = ::CreateFileW(imagePath.c_str(),
                                  GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return Error{.code = ErrorCode::kNotFound,
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

// Opens the image and determines its size as one unit; the caller only
// needs to know whether acquiring a usable, sized handle succeeded.
Result<std::pair<HANDLE, std::uint64_t>> openImage(const std::filesystem::path& imagePath) {
    const auto handle = openHandle(imagePath);
    if (!handle.hasValue()) {
        return handle.error();
    }
    const auto size = queryFileSize(handle.value());
    if (!size.hasValue()) {
        return size.error();
    }
    return std::pair{handle.value(), size.value()};
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
    ::CloseHandle(toHandle(nativeHandle_));
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
                                             std::bit_cast<std::intptr_t>(opened.value().first),
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
    return readFully(toHandle(nativeHandle_), offset, buffer.first(want));
}

} // namespace revenant

// NOLINTEND(misc-include-cleaner)
