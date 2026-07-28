// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Mounting one volume and walking its entries — the filesystem half
// of a hybrid run, kept apart from the sequencing that calls it. Not a public
// interface.

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"

namespace revenant::recovery {

// Mounts whatever filesystem the volume carries and reports what it holds.
// Which filesystem that is stays behind `fs::mountVolume`: nothing in the
// recovery layer names one.
[[nodiscard]] Result<fs::EnumerationStats>
enumerateVolume(BlockDevice& device, fs::EntryVisitor& visitor);

} // namespace revenant::recovery
