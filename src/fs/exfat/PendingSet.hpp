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
#include "revenant/fs/NameDecode.hpp"
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

// `parent/name`, or just `name` at the root.
[[nodiscard]] std::string joinedPath(const std::string& parent, const std::string& name);

// The reported entry, with its content located: through the table when the set
// used it, and as the contiguous run the set declared when it did not.
[[nodiscard]] RecoveredEntry
entryOf(const std::string& parentPath, const AssembledSet& set, const ClusterChain& chain);

} // namespace revenant::fs::exfat
