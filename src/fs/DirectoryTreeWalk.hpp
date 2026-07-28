// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The two things every cluster-chain filesystem's directory walk does
// the same way: reading a directory's clusters, and driving a worklist of
// directories to exhaustion.
//
// What a walk does with the *slots* it reads is where FAT32 and exFAT differ,
// and that stays in each filesystem. What is here is only the part that would
// otherwise be the same code twice. Not a public interface.

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "fs/ClusterChain.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs {

// Every byte of the directory occupying `clusters`, up to `capBytes`.
//
// A directory bigger than the cap is read up to it and no further: what a
// volume claims about its own size is data like any other (ADR-0009). A short
// read means the volume ends inside the directory, which makes the rest of it
// unreadable rather than empty.
[[nodiscard]] Result<std::vector<std::byte>> readDirectoryBytes(
	const ClusterChain& chain,
	std::span<const std::uint32_t> clusters,
	std::size_t capBytes);

// What a directory that will not read costs a walk. Skipping it is right — that
// region is what the carve pass is for — but a device that will not read is not
// a device with no files on it, so that one ends the walk.
[[nodiscard]] Result<std::uint64_t> skipUnreadableDirectory(const Error& error);

// One directory: read it, skip it if it will not read, and walk what came back.
// How a directory is read and what its slots mean both differ per filesystem;
// this sequence does not.
template <typename Read, typename WalkSlots>
[[nodiscard]] Result<std::uint64_t> walkOneDirectory(Read read, WalkSlots walkSlots) {
	const auto bytes = read();
	if (!bytes.hasValue()) {
		return skipUnreadableDirectory(bytes.error());
	}
	return walkSlots(bytes.value());
}

// Every whole slot of `bytes`, in order, with what each reported totalled. Both
// filesystems lay a directory out as an array of fixed-size slots; what a slot
// *means* is where they part company, and that stays with the caller.
template <typename Visit>
[[nodiscard]] std::uint64_t
foldSlots(std::span<const std::byte> bytes, std::size_t slotBytes, Visit visit) {
	std::uint64_t reported = 0;
	for (std::size_t at = 0; at + slotBytes <= bytes.size(); at += slotBytes) {
		reported += visit(bytes.subspan(at, slotBytes));
	}
	return reported;
}

// Runs `walkOne` over every cursor the worklist holds — including the ones
// `walkOne` adds while it runs — and totals what each reported. A walk keeps
// its own stack rather than recursing because every cluster number it follows
// came off the disk.
template <typename Cursor> [[nodiscard]] Cursor takeBack(std::vector<Cursor>& pending) {
	Cursor next = std::move(pending.back());
	pending.pop_back();
	return next;
}

template <typename Cursor, typename WalkOne>
[[nodiscard]] Result<std::uint64_t> driveWorklist(std::vector<Cursor>& pending, WalkOne walkOne) {
	std::uint64_t reported = 0;
	while (!pending.empty()) {
		const auto found = walkOne(takeBack(pending));
		if (!found.hasValue()) {
			return found.error();
		}
		reported += found.value();
	}
	return reported;
}

} // namespace revenant::fs
