// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"

namespace revenant::fs {

// Mounts whichever filesystem `device` carries.
//
// Every filesystem this build can read is offered the volume in turn. One that
// does not find its own signature declines, and the next is asked. One that
// *does* find it owns the answer: its parse failure is reported unchanged
// rather than passed over, because a corrupt NTFS volume is not an unknown
// volume, and treating it as one would send the run hunting for a FAT that was
// never there.
//
// `kNotFound` therefore means no filesystem recognized the volume at all — a
// formatted, RAW, or unsupported volume, which is exactly what the carve pass
// is for. A device that will not read keeps its own `kIoFailure`.
//
// The device is borrowed: it must outlive the returned filesystem.
[[nodiscard]] Result<std::unique_ptr<FileSystem>> mountVolume(BlockDevice& device);

} // namespace revenant::fs
