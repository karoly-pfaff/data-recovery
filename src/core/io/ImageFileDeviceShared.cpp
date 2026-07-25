// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"
#include "revenant/core/io/ReadRange.hpp"

// The three ImageFileDevice member-function bodies that need no
// platform-specific logic once the platform provides
// closeNative/readNative/acquireImage (declared in ReadRange.hpp, defined in
// ImageFileDevicePosix.cpp / ImageFileDeviceWindows.cpp). Living in this
// single, unconditionally-compiled translation unit — rather than as
// non-inline definitions in the shared header — keeps them genuinely
// ODR-safe: any TU may include ReadRange.hpp (e.g. a unit test for
// clampReadRange) without risking a duplicate-symbol link error.

namespace revenant {

ImageFileDevice::~ImageFileDevice() {
    closeNative(nativeHandle_);
}

Result<std::size_t> ImageFileDevice::readAt(std::uint64_t offset, std::span<std::byte> buffer) {
    const auto want = clampReadRange(offset, buffer.size(), sizeInBytes_);
    if (!want.hasValue()) {
        return want.error();
    }
    return readNative(nativeHandle_, offset, buffer.first(want.value()));
}

Result<std::unique_ptr<ImageFileDevice>>
ImageFileDevice::open(const std::filesystem::path& imagePath, std::uint32_t sectorSize) {
    if (sectorSize == 0) {
        return Error{.code = ErrorCode::kInvalidArgument};
    }
    const auto opened = acquireImage(imagePath);
    if (!opened.hasValue()) {
        return opened.error();
    }
    return std::make_unique<ImageFileDevice>(ImageFileDevice::ConstructTag{},
                                             opened.value().first,
                                             opened.value().second,
                                             sectorSize);
}

} // namespace revenant
