// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Reading the kernel's mount table, which is how a POSIX destination
// is traced to the device it is really written on (story-0609).
//
// The parsing is pure text, and compiled everywhere rather than only where
// `/proc` exists: getting the longest-covering-mount rule wrong is how a
// destination on the disk being recovered walks through the check, and that is
// not a thing to leave testable on one platform.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace revenant {

// The device the filesystem holding `path` was mounted from, per one
// `/proc/self/mountinfo` text: the source of the longest mount point that
// covers `path`. Nothing, when no mount point does.
//
// The *source*, deliberately, and not the `major:minor` the same line carries
// third. They are not the same device: btrfs, overlayfs and every other
// filesystem the kernel gives an anonymous number report one no block device
// owns, and reading that as the destination's storage answers "no local disk"
// for a directory sitting squarely on one. The source survives verbatim so a
// caller can tell a block device from `server:/export`.
[[nodiscard]] std::optional<std::string>
mountSourceFor(std::string_view mountInfo, const std::filesystem::path& path);

} // namespace revenant
