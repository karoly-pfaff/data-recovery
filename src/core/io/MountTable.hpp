// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Reading the kernel's mount table, which is how a POSIX destination
// is traced to the device it is really written on (story-0609).
//
// The parsing is pure text, and compiled everywhere rather than only where
// `/proc` exists: getting the longest-covering-mount rule wrong is how a
// destination on the disk being recovered walks through the check, and that is
// not a thing to leave testable on one platform.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace revenant {

// Whether a filesystem of this type holds no local block storage at all, so a
// destination on one occupies no disk and conflicts with nothing. Anything that
// is not on the list has to resolve to a real device or the run is refused —
// "I could not trace it" is not evidence of safety.
[[nodiscard]] bool holdsNoLocalStorage(std::string_view type);

// What a path's filesystem was mounted from.
struct MountSource {
	// The filesystem type â€” `ext4`, `btrfs`, `nfs4`, `overlay`. Kept because it
	// is the only thing that distinguishes "mounted from no local device"
	// (a share, a tmpfs â€” a real answer) from "mounted from something this
	// build cannot trace", which must refuse rather than be assumed harmless.
	std::string type;
	// What it was mounted from: `/dev/sda3`, `/dev/mapper/vg-root`,
	// `server:/export`, or the bare word a virtual filesystem uses.
	std::string source;
};

// The mount `path` is really on, per one `/proc/self/mountinfo` text.
//
// `fsDevice` is the device the filesystem holding `path` reports, **in
// `deviceKey` form** — not a raw `dev_t`, which packs the same two halves
// differently and would therefore match no line at all. It selects among the
// mounts that cover `path`, and it has to: depth alone picks a mount that has
// since been shadowed by one at a shallower point, which still appears in the
// table and still covers the path while holding none of it. Where no line
// carries that number the deepest covering mount is used, so a caller with no
// number to offer still gets the useful answer.
//
// The mount *source*, deliberately, and not the `major:minor` the same line
// carries third. They are not the same device: btrfs, overlayfs and every other
// filesystem the kernel gives an anonymous number report one no block device
// owns, and reading that as the destination's storage answers "no local disk"
// for a directory sitting squarely on one.
[[nodiscard]] std::optional<MountSource> mountSourceFor(
	std::string_view mountInfo,
	const std::filesystem::path& path,
	std::uint64_t fsDevice);

} // namespace revenant
