// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any bytes offered as an NTFS volume enumerate cleanly — entries or
// none — never a crash, an out-of-range read, or a walk that does not
// terminate. Everything the walk follows comes from the input: the `$MFT`'s own
// runlist, every record it addresses, and every parent chain those records
// claim, which is where a crafted cycle would hang a scan.
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/EntryEnumeration.hpp"
#include "revenant/fs/ntfs/MftTable.hpp"
#include "support/FuzzInput.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

// A small volume whose `$MFT` starts at cluster 1 — the shape
// tools/fuzz/make_seed_corpus.py writes its seed to. Fixed rather than parsed
// out of the input: a geometry the fuzzer had to synthesise first would leave
// the enumeration itself almost never reached.
constexpr revenant::fs::ntfs::NtfsGeometry kGeometry{
	.bytesPerSector = 512,
	.bytesPerCluster = 1024,
	.totalClusters = 1024,
	.mftOffsetBytes = 1024,
	.bytesPerMftRecord = 1024};

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
	revenant::testing::InMemoryDevice device{std::move(bytes), kGeometry.bytesPerSector};
	const auto table = revenant::fs::ntfs::MftTable::open(device, kGeometry);
	if (!table.hasValue()) {
		return 0;
	}
	NullVisitor visitor;
	static_cast<void>(revenant::fs::ntfs::enumerateEntries(table.value(), visitor));
	return 0;
}
