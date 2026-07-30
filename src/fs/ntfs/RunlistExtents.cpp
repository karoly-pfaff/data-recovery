// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cstdint>
#include <vector>

#include "fs/ExtentSpan.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/SafeArith.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/Runlist.hpp"

namespace revenant::fs::ntfs {

namespace {

// Proving the volume's byte size fits in 64 bits is what lets every offset
// derived from a run *inside* that volume be computed without a further check.
[[nodiscard]] Result<std::uint64_t> volumeByteSize(const NtfsGeometry& geometry) {
	return safeMul64(geometry.totalClusters, geometry.bytesPerCluster, 0);
}

[[nodiscard]] bool runFitsVolume(const DataRun& run, std::uint64_t totalClusters) noexcept {
	return run.startCluster <= totalClusters &&
		   run.lengthClusters <= totalClusters - run.startCluster;
}

// A sparse run has no bytes to point at, and a run reaching past the volume is
// not this file's data. Either one makes the whole runlist unusable.
[[nodiscard]] bool allRunsMappable(const Runlist& runlist, const NtfsGeometry& geometry) {
	return std::ranges::none_of(runlist.runs, [&geometry](const DataRun& run) {
		return run.sparse || !runFitsVolume(run, geometry.totalClusters);
	});
}

[[nodiscard]] Extent runExtent(const DataRun& run, const NtfsGeometry& geometry) noexcept {
	const auto clusterBytes = static_cast<std::uint64_t>(geometry.bytesPerCluster);
	return Extent{
		.deviceOffset = run.startCluster * clusterBytes,
		.lengthBytes = run.lengthClusters * clusterBytes};
}

// Every run as a full-length extent; trimming to the declared size is a
// separate step below.
[[nodiscard]] Result<std::vector<Extent>>
mapRuns(const Runlist& runlist, const NtfsGeometry& geometry) {
	if (!allRunsMappable(runlist, geometry)) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	std::vector<Extent> extents;
	extents.reserve(runlist.runs.size());
	for (const auto& run : runlist.runs) {
		extents.push_back(runExtent(run, geometry));
	}
	return extents;
}

} // namespace

Result<std::vector<Extent>>
runlistExtents(const Runlist& runlist, const NtfsGeometry& geometry, std::uint64_t realSize) {
	return volumeByteSize(geometry)
		.andThen([&](const std::uint64_t&) { return mapRuns(runlist, geometry); })
		.andThen([realSize](const std::vector<Extent>& extents) {
			return trimToSize(extents, realSize);
		});
}

} // namespace revenant::fs::ntfs
