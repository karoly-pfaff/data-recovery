// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant {

inline constexpr std::uint32_t kDefaultSectorSize = 512;

// Read-only BlockDevice over a raw image file (.dd/.img), the portable,
// privilege-free source used throughout development. Positioned reads only —
// no shared mutable file offset, so concurrent readAt calls are safe.
// Platform I/O lives in ImageFileDevicePosix.cpp / ImageFileDeviceWindows.cpp,
// selected by CMake (no #ifdef in shared code).
class ImageFileDevice final : public BlockDevice {
public:
    // Opens `imagePath` read-only. Missing/unreadable path -> kNotFound;
    // sectorSize == 0 -> kInvalidArgument.
    [[nodiscard]] static Result<std::unique_ptr<ImageFileDevice>>
    open(const std::filesystem::path& imagePath, std::uint32_t sectorSize = kDefaultSectorSize);

    ~ImageFileDevice() override;
    // BlockDevice already deletes copy/move; restated here (rule of five)
    // because this class declares its own destructor.
    ImageFileDevice(const ImageFileDevice&) = delete;
    ImageFileDevice& operator=(const ImageFileDevice&) = delete;
    ImageFileDevice(ImageFileDevice&&) = delete;
    ImageFileDevice& operator=(ImageFileDevice&&) = delete;

    [[nodiscard]] std::uint64_t sizeInBytes() const override {
        return sizeInBytes_;
    }

    [[nodiscard]] std::uint32_t sectorSize() const override {
        return sectorSize_;
    }

    [[nodiscard]] Result<std::size_t> readAt(std::uint64_t offset,
                                             std::span<std::byte> buffer) override;

    // Construction goes through open(); the tag blocks direct outside use
    // while keeping the constructor public for std::make_unique. Defined
    // here (not per-platform): storing three primitives is platform-neutral,
    // so duplicating it in both ImageFileDevicePosix.cpp and
    // ImageFileDeviceWindows.cpp would just be a jscpd clone waiting to
    // happen.
    struct ConstructTag {};

    // NOLINTBEGIN(bugprone-easily-swappable-parameters) - nativeHandle/
    // sizeBytes/sectorSize are three adjacent, mutually-convertible integral
    // types, but ConstructTag guards this constructor to open()'s single
    // call site (the make_unique call in
    // src/core/io/ImageFileDeviceShared.cpp), so the swap risk this check
    // targets does not apply.
    ImageFileDevice(ConstructTag /*unused*/,
                    std::intptr_t nativeHandle,
                    std::uint64_t sizeBytes,
                    std::uint32_t sectorSize) noexcept
        : nativeHandle_(nativeHandle), sizeInBytes_(sizeBytes), sectorSize_(sectorSize) {}

    // NOLINTEND(bugprone-easily-swappable-parameters)

private:
    std::intptr_t nativeHandle_;
    std::uint64_t sizeInBytes_;
    std::uint32_t sectorSize_;
};

} // namespace revenant
