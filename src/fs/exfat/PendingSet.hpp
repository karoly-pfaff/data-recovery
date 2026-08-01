// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. An exFAT entry set under construction: a file entry, a stream
// extension and however many name fragments follow, gathered as the walk passes
// over them and handed back as one file. Not a public interface.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "fs/ClusterChain.hpp"
#include "fs/exfat/AllocationBitmap.hpp"
#include "revenant/core/Utf16Name.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/exfat/DirectoryEntry.hpp"

namespace revenant::fs::exfat {

// One complete set, as the walk wants it.
struct AssembledSet {
	DecodedName name;
	std::uint32_t firstCluster{};
	std::uint64_t sizeInBytes{};
	Timestamps timestamps{};
	bool isDirectory{};
	bool inUse{};
	// The set said its clusters follow one another, so the table holds nothing
	// for it. For a deleted file that is a stated extent rather than a guess.
	bool contiguous{};
};

class PendingSet {
public:
	// Starts a set at its file entry, discarding whatever was half-collected —
	// a set interrupted by the next one was never going to complete.
	void begin(bool inUse, std::span<const std::byte> slot);

	// Folds one following entry in. Anything that is not part of a file's set —
	// the bitmap, the up-case table, the volume label, a type this build does
	// not read — is ignored rather than guessed at.
	void absorb(ExfatEntryKind kind, std::span<const std::byte> slot);

	// The finished set, or nothing when none was started or the one started
	// never got the stream extension that says where its bytes are.
	[[nodiscard]] std::optional<AssembledSet> take();

private:
	void takeStream(std::span<const std::byte> slot);
	void appendName(std::span<const std::byte> slot);

	bool active_ = false;
	bool inUse_ = false;
	bool hasStream_ = false;
	FileEntry file_;
	StreamExtension stream_;
	std::vector<std::byte> nameBytes_;
};

// Where a set's content is looked for, and what the volume says about the
// clusters it claims.
struct SetSource {
	const ClusterChain* chain;
	const AllocationBitmap* bitmap;
};

// The reported entry, with its content located: through the table when the set
// used it, and as the contiguous run the set declared when it did not.
//
// A *deleted* set whose first cluster the bitmap says is in use again has had
// its bytes handed to something else. Its name is still real, so the entry is
// reported — but with no extents, because handing back bytes that now belong to
// a live file would be worse than handing back none. That region is what the
// carve pass is for.
[[nodiscard]] RecoveredEntry
entryOf(const std::string& parentPath, const AssembledSet& set, const SetSource& source);

} // namespace revenant::fs::exfat
