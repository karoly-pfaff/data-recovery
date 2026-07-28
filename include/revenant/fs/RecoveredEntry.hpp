// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "revenant/core/Confidence.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs {

// One file a filesystem pass found, with everything needed to write it out —
// and nothing written yet (ADR-0006). Filesystem-layer vocabulary, not NTFS's:
// every filesystem reports this shape.
struct RecoveredEntry {
	// Volume-relative, '/'-separated UTF-8. A *logical* path inside the volume,
	// never a host path: it becomes one only through `sanitizeOutputPath`
	// (ADR-0009), which is the single place a name may reach the filesystem.
	std::string path;
	std::uint64_t sizeInBytes{};

	// Where the content is. Exactly one of these carries it. A small file's
	// bytes live inside its metadata record, where the update-sequence fixup
	// interrupts them — so they cannot be named as a device extent and are
	// carried as parsed instead. Both stay empty when the metadata is intact
	// but the content could not be located; the region is then carve territory.
	std::vector<Extent> extents;
	std::vector<std::byte> residentContent;

	Timestamps timestamps{};
	EntryState state{};

	// How far the metadata can be trusted, on the shared verdict scale. This is
	// the handoff point to the carve pass: anything short of `kValid` means the
	// region is scanned rather than taken on the filesystem's word.
	Confidence recoverability{};
};

// Receives every entry an enumeration discovers. Implementations decide what
// discovery means (collect, index, count); enumeration never extracts.
class EntryVisitor {
public:
	virtual ~EntryVisitor() = default;
	EntryVisitor() = default;
	EntryVisitor(const EntryVisitor&) = delete;
	EntryVisitor& operator=(const EntryVisitor&) = delete;
	EntryVisitor(EntryVisitor&&) = delete;
	EntryVisitor& operator=(EntryVisitor&&) = delete;

	virtual void onEntry(const RecoveredEntry& entry) = 0;
};

} // namespace revenant::fs
