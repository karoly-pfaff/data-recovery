// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"

namespace revenant::fs::ext4 {

// An entry's fixed part: the inode it names, how far the next entry is, how long
// its name is, and what kind of thing it points at. The name follows, unpadded.
inline constexpr std::size_t kDirEntryHeaderBytes = 8;

// What the entry says its inode holds. ext4 always carries this — the feature
// that adds it has been mandatory since ext3 — but it is the *directory's*
// claim, and the inode is what settles it.
enum class Ext4FileType : std::uint8_t { kUnknown, kRegularFile, kDirectory, kOther };

// One linear directory entry.
//
// `recordBytes` is the distance to the next entry, which is not the same as this
// entry's own length. Deleting a file in ext4 does not blank its entry: it adds
// the entry's record length to the *previous* entry's, so the previous record
// swallows it and no reader walking record to record ever sees it again. The
// deleted name is still lying in that hole — which is why ext4 name recovery is
// partial rather than full, and why finding it is a search rather than a read.
struct Ext4DirEntry {
	std::uint32_t inode{};
	std::uint16_t recordBytes{};
	Ext4FileType type{};
	// Raw, undecoded: ext4 enforces no encoding, so the bytes are handed on as
	// they lie and `fs::decodeRawName` decides what they mean (ADR-0010).
	std::vector<std::byte> nameBytes;
};

// Parses the entry at the front of `bytes`.
//
// Input shorter than the fixed part is `kOutOfRange`. A record length below 8,
// not a multiple of 4, or reaching past `bytes` is `kInvalidArgument` at `0x04`
// — a record that does not point at a real next entry ends the walk rather than
// sending it somewhere arbitrary. A name that will not fit inside its own record
// is `kInvalidArgument` at `0x06`.
[[nodiscard]] Result<Ext4DirEntry> parseExt4DirEntry(std::span<const std::byte> bytes);

} // namespace revenant::fs::ext4
