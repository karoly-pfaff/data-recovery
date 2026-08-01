// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Walking a sysfs block tree (story-0609): what storage the device
// at `<root>/<major>:<minor>` covers.
//
// Neutral of the platform that owns such a tree, so the part that decides —
// the worklist, the descent through stacked devices, the bounds, what refuses —
// is testable against a fixture rather than only against a running kernel. The
// `dev_t` arithmetic that is genuinely POSIX stays in SysfsStoragePosix.cpp.

#include <filesystem>
#include <string>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"

namespace revenant {

// The storage the node named `nodeName` under `root` covers: a partition's
// window on the disk carrying it, or a whole disk, or — for a device built on
// other devices — the union of what those cover.
//
// Anything the tree will not answer refuses instead of resolving to less. A
// smaller claim than the truth is what lets a destination on the source
// through, so every unknown here fails toward refusing the run.
[[nodiscard]] Result<StorageExtents>
storageUnderSysfs(const std::filesystem::path& root, const std::string& nodeName);

} // namespace revenant
