// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery/VolumeWalk.hpp"

#include <memory>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/Mount.hpp"
#include "revenant/fs/RecoveredEntry.hpp"

namespace revenant::recovery {

Result<fs::EnumerationStats> enumerateVolume(BlockDevice& device, fs::EntryVisitor& visitor) {
	return fs::mountVolume(device).andThen(
		[&visitor](const std::unique_ptr<fs::FileSystem>& volume) {
			return volume->enumerate(visitor);
		});
}

} // namespace revenant::recovery
