// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

#include "fs/SafeArith.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
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

// Appends `extent` shortened to what the file still needs, and reports what is
// still missing after it. A fully consumed size drops the remaining runs.
[[nodiscard]] std::uint64_t
takeExtent(std::vector<Extent>& extents, const Extent& extent, std::uint64_t remaining) {
	const auto take = std::min(extent.lengthBytes, remaining);
	if (take == 0) {
		return 0;
	}
	extents.push_back(Extent{.deviceOffset = extent.deviceOffset, .lengthBytes = take});
	return remaining - take;
}

[[nodiscard]] std::vector<Extent>
takeBytes(const std::vector<Extent>& extents, std::uint64_t realSize) {
	std::vector<Extent> taken;
	std::uint64_t remaining = realSize;
	for (const auto& extent : extents) {
		remaining = takeExtent(taken, extent, remaining);
	}
	return taken;
}

[[nodiscard]] std::uint64_t coveredBytes(const std::vector<Extent>& extents) noexcept {
	return std::accumulate(
		extents.begin(),
		extents.end(),
		std::uint64_t{0},
		[](std::uint64_t sum, const Extent& extent) { return sum + extent.lengthBytes; });
}

// The last cluster of a file is only partly used, so the extents are cut back
// to the declared size. Claiming more bytes than the runs allocate is a lie.
[[nodiscard]] Result<std::vector<Extent>>
trimToRealSize(const std::vector<Extent>& extents, std::uint64_t realSize) {
	auto trimmed = takeBytes(extents, realSize);
	if (coveredBytes(trimmed) < realSize) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	return trimmed;
}

} // namespace

Result<std::vector<Extent>>
runlistExtents(const Runlist& runlist, const NtfsGeometry& geometry, std::uint64_t realSize) {
	return volumeByteSize(geometry)
		.andThen([&](const std::uint64_t&) { return mapRuns(runlist, geometry); })
		.andThen([realSize](const std::vector<Extent>& extents) {
			return trimToRealSize(extents, realSize);
		});
}

} // namespace revenant::fs::ntfs
