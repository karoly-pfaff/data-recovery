// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ext4/DirectoryHole.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "revenant/fs/ext4/DirectoryEntry.hpp"

namespace revenant::fs::ext4 {

namespace {

// Every record starts on a four-byte boundary, so the search only ever looks at
// positions ext4 could have written one at.
constexpr std::size_t kAlignment = 4;

[[nodiscard]] std::size_t alignUp(std::size_t value) {
	return ((value + kAlignment - 1) / kAlignment) * kAlignment;
}

// A NUL or a `/` can never appear in an ext4 name, so either one says these
// bytes are padding or content rather than a name.
[[nodiscard]] bool plausibleName(std::span<const std::byte> name) {
	return !name.empty() && std::ranges::none_of(name, [](std::byte raw) {
		return raw == std::byte{0} || raw == std::byte{'/'};
	});
}

[[nodiscard]] bool plausible(const Ext4DirEntry& entry, std::uint32_t inodeCount) {
	return entry.inode != 0 && entry.inode <= inodeCount && plausibleName(entry.nameBytes);
}

// One candidate position. Returns how far the search resumes past it: over a
// found entry's own bytes, so a second deletion behind the first is still
// reachable, or one alignment step when nothing was there.
[[nodiscard]] std::size_t takeCandidate(
	std::vector<HoleEntry>& found,
	std::span<const std::byte> at,
	std::uint32_t inodeCount) {
	auto parsed = parseExt4DirEntry(at);
	if (!parsed.hasValue() || !plausible(parsed.value(), inodeCount)) {
		return kAlignment;
	}
	const auto nameLength = parsed.value().nameBytes.size();
	found.push_back(
		HoleEntry{.inode = parsed.value().inode, .nameBytes = std::move(parsed.value().nameBytes)});
	return liveEntryBytes(nameLength);
}

} // namespace

std::size_t liveEntryBytes(std::size_t nameLength) {
	return alignUp(kDirEntryHeaderBytes + nameLength);
}

std::vector<HoleEntry>
deletedEntriesIn(std::span<const std::byte> record, const HoleBounds& bounds) {
	std::vector<HoleEntry> found;
	std::size_t at = alignUp(bounds.liveBytes);
	while (at + kDirEntryHeaderBytes <= record.size()) {
		at += takeCandidate(found, record.subspan(at), bounds.inodeCount);
	}
	return found;
}

} // namespace revenant::fs::ext4
