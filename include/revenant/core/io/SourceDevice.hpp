// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <memory>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant {

// Opens whatever `source` names, as the one `BlockDevice` a run reads through:
// an `ImageFileDevice` when the path is a regular file, and a `RawDevice` — a
// whole disk or a volume — when it is not.
//
// "Is it a regular file?" is the right question rather than a convenient one. A
// whole disk and a mounted volume are precisely the things a filesystem reports
// as *not* regular files, on both platforms, so this layer never has to learn
// how a device path is spelled — no `\\.\PhysicalDrive` prefix, no `/dev`
// convention, nothing to keep in step with an OS that adds another.
//
// A path that names nothing at all falls to the device branch and comes back as
// that branch's failure, which is the same kNotFound either way.
[[nodiscard]] Result<std::unique_ptr<BlockDevice>> openSource(const std::filesystem::path& source);

} // namespace revenant
