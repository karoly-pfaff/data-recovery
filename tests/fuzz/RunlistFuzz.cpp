// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/Runlist.hpp"

namespace {

// A plausible mid-size volume, so a decoded runlist has a real chance of
// mapping instead of being rejected by geometry on every input.
constexpr revenant::fs::ntfs::NtfsGeometry kGeometry{
	.bytesPerSector = 512,
	.bytesPerCluster = 4096,
	.totalClusters = 1U << 20U,
	.mftOffsetBytes = 0,
	.bytesPerMftRecord = 1024};

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const auto bytes = std::as_bytes(std::span{data, size});
	const auto runlist = revenant::fs::ntfs::decodeRunlist(bytes);
	if (!runlist.hasValue()) {
		return 0;
	}
	// The declared size comes from the same bytes on disk, so vary it with the
	// input rather than always asking for the full allocation.
	(void)revenant::fs::ntfs::runlistExtents(runlist.value(), kGeometry, size);
	return 0;
}
