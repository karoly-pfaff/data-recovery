// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ntfs/NtfsLayout.hpp"

#include <cstdint>

namespace revenant::imagegen::ntfs {

namespace {

// A 4 MiB volume: large enough for an MFT, a directory tree, fragmented file
// data, and unallocated space to carve from; small enough to build in memory
// and to keep as a checked-in expectation.
constexpr std::uint32_t kBytesPerSector = 512;
constexpr std::uint32_t kSectorsPerCluster = 8;
constexpr std::uint64_t kTotalClusters = 1024;
constexpr std::uint64_t kMftStartCluster = 8;
constexpr std::uint32_t kMftRecordBytes = 1024;

// The volume with only its `$MFT` sized: everything else follows from it, so
// scaling the record count moves the volume's end and nothing before it.
[[nodiscard]] NtfsLayout planFor(std::uint32_t mftRecordCount) noexcept {
	return NtfsLayout{
		.bytesPerSector = kBytesPerSector,
		.sectorsPerCluster = kSectorsPerCluster,
		.totalClusters = kTotalClusters,
		.mftStartCluster = kMftStartCluster,
		.mftRecordBytes = kMftRecordBytes,
		.mftRecordCount = mftRecordCount};
}

// How many clusters the fixture's files and its unallocated space occupy, read
// off the fixed plan rather than restated, so the two cannot drift apart.
[[nodiscard]] std::uint64_t dataClusterCount() noexcept {
	const auto fixed = planFor(kMftRecordCount);
	return kTotalClusters - fixed.dataStartCluster();
}

} // namespace

std::uint32_t NtfsLayout::bytesPerCluster() const noexcept {
	return bytesPerSector * sectorsPerCluster;
}

std::uint64_t NtfsLayout::clusterOffsetBytes(std::uint64_t cluster) const noexcept {
	return cluster * bytesPerCluster();
}

std::uint64_t NtfsLayout::mftOffsetBytes() const noexcept {
	return clusterOffsetBytes(mftStartCluster);
}

std::uint64_t NtfsLayout::mftClusterCount() const noexcept {
	const std::uint64_t mftBytes = std::uint64_t{mftRecordCount} * mftRecordBytes;
	return (mftBytes + bytesPerCluster() - 1) / bytesPerCluster();
}

std::uint64_t NtfsLayout::dataStartCluster() const noexcept {
	return mftStartCluster + mftClusterCount();
}

std::uint64_t NtfsLayout::totalBytes() const noexcept {
	return totalClusters * bytesPerCluster();
}

NtfsLayout makeLayout() noexcept {
	return planFor(kMftRecordCount);
}

NtfsLayout makeLayoutForRecords(std::uint32_t mftRecordCount) noexcept {
	NtfsLayout scaled = planFor(mftRecordCount);
	scaled.totalClusters = scaled.dataStartCluster() + dataClusterCount();
	return scaled;
}

} // namespace revenant::imagegen::ntfs
