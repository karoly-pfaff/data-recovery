// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Mounting one volume and walking its entries — the filesystem half
// of a hybrid run, kept apart from the sequencing that calls it. Not a public
// interface.

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/ntfs/EntryEnumeration.hpp"

namespace revenant::recovery {

// The vertical slice has one filesystem; the seam that makes this polymorphic
// arrives with the second one (M3).
[[nodiscard]] Result<fs::ntfs::EnumerationStats>
enumerateVolume(BlockDevice& device, fs::EntryVisitor& visitor);

} // namespace revenant::recovery
