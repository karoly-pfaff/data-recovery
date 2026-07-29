// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. ext4's jbd2 journal, read as a record of what the volume was about
// to write — and never replayed (ADR-0005). Replay is a write; what is taken
// from here is a *hint*, an older copy of some bytes, used only to locate
// content the live metadata no longer locates. Not a public interface.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fs/ext4/BlockReader.hpp"
#include "fs/ext4/InodeTable.hpp"

namespace revenant::fs::ext4 {

// A journal is read this far and no further, however large it says it is
// (ADR-0009). Eight thousand blocks is more than the default journal on any
// volume, and a journal claiming more is claiming reads rather than data.
inline constexpr std::uint64_t kMaxJournalBlocks = 8192;
inline constexpr std::size_t kMaxJournalCopies = 1U << 16U;

// Where the journal holds one copy of one filesystem block.
struct JournalCopy {
	std::uint64_t fileSystemBlock;
	std::uint64_t deviceOffset;
};

class Journal {
public:
	Journal() = default;
	explicit Journal(std::vector<JournalCopy> copies) noexcept;

	// False when there is no journal to consult, in which case nothing may be
	// concluded from it either way.
	[[nodiscard]] bool known() const noexcept;

	// Every device offset at which the journal still holds a copy of `block`,
	// newest transaction last — the order the journal wrote them.
	[[nodiscard]] std::vector<std::uint64_t> copiesOf(std::uint64_t block) const;

private:
	std::vector<JournalCopy> copies_;
};

// Reads the volume's journal and indexes every copy it still holds.
//
// A journal that is absent, unreadable, laid out for a different block size, or
// written with a feature this build does not implement leaves an unknown
// journal: the walk goes on without the hint rather than on a guess.
[[nodiscard]] Journal readJournal(const Ext4Blocks& blocks, const Ext4InodeTable& inodes);

} // namespace revenant::fs::ext4
