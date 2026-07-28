// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/fs/NameDecode.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs::fat {

// A FAT directory is an array of these, in the file data area like any other
// content.
inline constexpr std::size_t kDirectoryEntryBytes = 32;

// What one 32-byte slot is. A deleted file is not a kind of its own: the
// deletion marker overwrites a name byte and leaves everything else standing,
// so a deleted file is a `kFile` that says so.
enum class EntryKind : std::uint8_t {
	// The first byte is 0x00: this slot has never been used, and by FAT's rule
	// neither has any slot after it. A walk stops here.
	kEndOfDirectory,
	// A fragment of the name belonging to the short entry that follows it.
	kLongName,
	// The volume label, which lives in the root directory and names no file.
	kVolumeLabel,
	kDirectory,
	kFile,
};

// A short (8.3) directory entry, parsed.
struct ShortEntry {
	// The 8.3 name decoded to UTF-8. A deleted entry's first character was
	// overwritten by the deletion marker, so it comes back as a placeholder and
	// `name.lossless` is false.
	DecodedName name;
	// FAT splits the cluster number across two fields; this is the whole of it.
	std::uint32_t firstCluster{};
	std::uint32_t sizeInBytes{};
	Timestamps timestamps{};
	bool deleted{};
};

// One fragment of a long name. Fragments precede the short entry they belong
// to, in reverse order, so assembling a name needs the directory walk rather
// than the slot.
struct LongNameFragment {
	// Its 1-based place in the name, with the last-fragment flag stripped out.
	// Meaningless when `deleted`: the marker overwrote the byte both live in.
	std::uint8_t ordinal{};
	// The flag: set on the fragment that holds the *end* of the name, which is
	// the one physically first. Meaningless when `deleted`, for the same reason.
	bool last{};
	// Deletion overwrites the ordinal byte of *every* slot in an entry set, so
	// a deleted file's fragments still hold their characters but no longer say
	// where they go. Physical order is all that is left to assemble them by.
	bool deleted{};
	// Of the short name this belongs to. It cannot be verified against a
	// deleted entry, whose short name lost a byte — which is why a long name
	// recovered from a deleted file is evidence, not proof.
	std::uint8_t checksum{};
	// The fragment's UTF-16LE code units in name order, gathered from the three
	// runs the slot splits them across and cut at the NUL that ends a name
	// shorter than the fragment. Bytes rather than characters so the assembler
	// concatenates fragments and decodes the whole name once (ADR-0010).
	std::vector<std::byte> nameBytes;
};

// Which kind of slot this is. Truncated input yields kOutOfRange.
[[nodiscard]] Result<EntryKind> classifyEntry(std::span<const std::byte> slot);

// Parses a slot `classifyEntry` called a file, directory or volume label.
// Truncated input yields kOutOfRange.
[[nodiscard]] Result<ShortEntry> parseShortEntry(std::span<const std::byte> slot);

// Parses a slot `classifyEntry` called a long-name fragment. Truncated input
// yields kOutOfRange; an ordinal of zero — which no fragment may carry, and
// which would make the assembled order meaningless — yields kInvalidArgument.
[[nodiscard]] Result<LongNameFragment> parseLongNameFragment(std::span<const std::byte> slot);

} // namespace revenant::fs::fat
