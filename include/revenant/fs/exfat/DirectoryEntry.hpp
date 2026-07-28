// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs::exfat {

// An exFAT directory is an array of these, in the cluster heap like any other
// content.
inline constexpr std::size_t kDirectoryEntryBytes = 32;

// A file is not one entry but a *set*: a file entry, a stream extension, and as
// many name fragments as its name needs, in that order. This names one slot's
// part in that; assembling the set is the walk's job.
enum class ExfatEntryKind : std::uint8_t {
	// The type byte is zero: this slot has never been used, and by exFAT's rule
	// neither has any slot after it.
	kEndOfDirectory,
	// Opens a set: attributes, timestamps, and how many entries follow.
	kFile,
	// Second in a set: where the content is and how much of it there is.
	kStreamExtension,
	// Third and after: 15 UTF-16 code units of the name.
	kFileName,
	// The volume's allocation bitmap — which clusters are in use.
	kAllocationBitmap,
	kUpcaseTable,
	kVolumeLabel,
	// A type this build does not read. Skipped, never guessed at.
	kUnknown,
};

// What a slot is, and whether it is still in use. exFAT marks a set deleted by
// clearing bit 7 of *every* type byte in it, which — unlike FAT's `0xE5` — takes
// no part of the name with it. That is why exFAT name recovery is full.
struct ExfatEntryHeader {
	ExfatEntryKind kind;
	bool inUse;
};

// The entry that opens a set.
struct FileEntry {
	// How many entries follow this one in the set: one stream extension plus
	// the name fragments. A set claiming more than a directory can hold is the
	// walk's problem, not this parser's.
	std::uint8_t secondaryCount{};
	Timestamps timestamps{};
	bool isDirectory{};
};

// The entry that says where a file's bytes are.
struct StreamExtension {
	std::uint32_t firstCluster{};
	// What the file actually holds, as opposed to what is allocated for it.
	std::uint64_t validDataLength{};
	std::uint64_t dataLength{};
	// How many UTF-16 code units the name fragments carry between them.
	std::uint8_t nameLength{};
	// exFAT lets a contiguous file say its clusters follow one another and skip
	// the FAT entirely. For a *deleted* file that is the difference between a
	// stated extent and a guess — which is why exFAT undelete beats FAT32's.
	bool noFatChain{};
};

// Which slot this is, and whether its set is still in use. Truncated input
// yields kOutOfRange.
[[nodiscard]] Result<ExfatEntryHeader> classifyExfatEntry(std::span<const std::byte> slot);

[[nodiscard]] Result<FileEntry> parseFileEntry(std::span<const std::byte> slot);
[[nodiscard]] Result<StreamExtension> parseStreamExtension(std::span<const std::byte> slot);

// A name fragment's 15 UTF-16LE code units, as raw bytes so the walk can
// concatenate fragments and decode the whole name once (ADR-0010).
[[nodiscard]] Result<std::vector<std::byte>> parseFileName(std::span<const std::byte> slot);

} // namespace revenant::fs::exfat
