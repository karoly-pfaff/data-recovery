// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any bytes offered as a volume mount and enumerate cleanly —
// entries or none — never a crash, an out-of-range read, or a walk that does
// not terminate. Everything the walk follows comes from the input: the BPB's
// geometry, every FAT entry a chain runs through, every directory slot, and
// every subdirectory those slots point at, which is where a crafted cycle would
// hang a scan.
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
