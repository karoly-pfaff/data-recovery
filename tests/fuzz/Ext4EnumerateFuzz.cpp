// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any bytes offered as an ext4 volume mount and enumerate cleanly —
// entries or none — never a crash, an out-of-range read, or a walk that does not
// terminate. Everything the walk follows comes from the input: the superblock's
// geometry, the group descriptor's inode table, every extent tree it reads a
// node of, every directory record's length, every inode a record names, every
// candidate the hole search accepts, the orphan chain, and the journal's own
// descriptor blocks. Each of those is a place a crafted volume could send the
// walk in a circle.
//
// The mount table is entered rather than ext4 directly, so a input that another
// filesystem claims first is exercised the way a real run would exercise it.
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "revenant/fs/Mount.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "support/FuzzInput.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

constexpr std::uint32_t kSectorSize = 512;

class NullVisitor final : public revenant::fs::EntryVisitor {
public:
	void onEntry(const revenant::fs::RecoveredEntry& entry) override {
		static_cast<void>(entry);
	}
};

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	auto bytes = revenant::testing::toByteVector(std::span<const std::uint8_t>{data, size});
	revenant::testing::InMemoryDevice device{std::move(bytes), kSectorSize};
	const auto mounted = revenant::fs::mountVolume(device);
	if (!mounted.hasValue()) {
		return 0;
	}
	NullVisitor visitor;
	static_cast<void>(mounted.value()->enumerate(visitor));
	return 0;
}
