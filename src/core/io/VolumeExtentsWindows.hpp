// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal, Windows. Asking a volume or a device where its bytes physically are
// (story-0609). Naming the volume a *path* sits on is the other half, and lives
// in DeviceIdentityWindows.cpp.

#include <string>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"

namespace revenant {

// The disk extents of the volume `volumePath` opens — one for a plain
// partition, several for a spanned volume, so the set is the identity and
// spanned storage is covered without knowing it exists.
[[nodiscard]] Result<StorageExtents> storageOfVolume(const std::wstring& volumePath);

// What a raw device path reads: every byte of a whole disk, or one volume's
// extents. `\\.\PhysicalDriveN` is the first kind and `\\.\C:` the second.
[[nodiscard]] Result<StorageExtents> storageOfDevicePath(const std::wstring& devicePath);

} // namespace revenant
