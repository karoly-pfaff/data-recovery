// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The deleted entries ext4 leaves lying inside its live ones.
//
// Deleting a file here does not mark its entry the way FAT's `0xE5` or exFAT's
// in-use bit does. ext4 adds the deleted entry's record length to the *previous*
// entry's, so that record swallows it and every reader walking record to record
// steps straight over it. The bytes are still there. Finding them is therefore a
// search — over bytes that may equally be a name's tail, a stale record, or
// nothing — which is why ext4 name recovery is partial and why what is found is
// evidence rather than a record. Not a public interface.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace revenant::fs::ext4 {

// A deleted entry recovered from the hole behind a live one.
struct HoleEntry {
	std::uint32_t inode;
	std::vector<std::byte> nameBytes;
};

// How many bytes a live entry with a name of `nameLength` occupies, rounded up
// as ext4 rounds it. Everything after that inside its record is hole.
[[nodiscard]] std::size_t liveEntryBytes(std::size_t nameLength);

// Where the hole starts and what makes a candidate in it believable. Named
// fields, because "how many bytes the live entry took" and "how many inodes the
// volume has" are two counts a call site could hand over the wrong way round.
struct HoleBounds {
	std::size_t liveBytes;
	std::uint32_t inodeCount;
};

// Every plausible deleted entry lying in `record` after its live entry's own
// bytes.
//
// A candidate must name an inode the volume could have, fit inside the record,
// and carry a name made of bytes a name can be made of. Any four aligned bytes
// can be read as an inode number, so these are what keep the search from
// inventing files out of padding — and even a candidate that passes them is
// graded uncertain by whoever reports it.
[[nodiscard]] std::vector<HoleEntry>
deletedEntriesIn(std::span<const std::byte> record, const HoleBounds& bounds);

} // namespace revenant::fs::ext4
