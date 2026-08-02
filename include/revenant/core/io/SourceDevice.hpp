// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/SourceStack.hpp"

namespace revenant {

// What a source path turns out to name.
enum class SourceKind : std::uint8_t {
	kImageFile,           // a regular file, opened as an ImageFileDevice
	kDevice,              // a whole disk or a volume, opened as a RawDevice
	kNotBlockAddressable, // a directory, which exposes only live files
};

// The choice `openSource` makes, answered on its own. ADR-0005's destination
// rule applies a different test to an image than to a device — a path-spelling
// rule against the first, physical identity against the second — and it has to
// reach that verdict the same way the open does. One classification, two
// callers, rather than two answers that can drift apart (story-0609).
[[nodiscard]] SourceKind classifySource(const std::filesystem::path& source);

// Opens whatever `source` names, as the composed stack a run reads through: an
// `ImageFileDevice` when the path is a regular file, and a `RawDevice` — a whole
// disk or a volume — when it is not, wrapped in the retry and cache layers by
// `SourceStack::over`.
//
// There is no bare-device path and no flag for one. An image on a network share
// wants the retry-and-cache treatment as much as a failing disk does (ADR-0007
// says so in as many words), and a production path without the decorators is the
// path story-0604 exists to retire: the map of what a run had to invent is only
// honest if every run has one.
//
// "Is it a regular file?" is the right question rather than a convenient one. A
// whole disk and a mounted volume are precisely the things a filesystem reports
// as *not* regular files, on both platforms, so this layer never has to learn
// how a device path is spelled — no `\\.\PhysicalDrive` prefix, no `/dev`
// convention, nothing to keep in step with an OS that adds another.
//
// A *directory* is refused outright as kNotBlockAddressable (ADR-0007,
// story-0406): a share root, a mounted NFS or SMB path and a plain folder all
// expose only live files, and recovery needs the bytes underneath them. An image
// file *on* a share is unaffected — it is a regular file, and being reached over
// a network is a latency problem rather than a capability one.
//
// A path that names nothing at all falls to the device branch and comes back as
// that branch's failure, which is the same kNotFound either way.
[[nodiscard]] Result<SourceStack> openSource(const std::filesystem::path& source);

} // namespace revenant
