// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace revenant::imagegen::ntfs {

// The geometry of the synthetic volume. Every derived offset is computed here,
// so no builder and no test spells out a byte position of its own.
struct NtfsLayout {
	std::uint32_t bytesPerSector;
	std::uint32_t sectorsPerCluster;
	std::uint64_t totalClusters;
	std::uint64_t mftStartCluster;
	std::uint32_t mftRecordBytes;
	std::uint32_t mftRecordCount;

	[[nodiscard]] std::uint32_t bytesPerCluster() const noexcept;
	[[nodiscard]] std::uint64_t clusterOffsetBytes(std::uint64_t cluster) const noexcept;
	[[nodiscard]] std::uint64_t mftOffsetBytes() const noexcept;
	[[nodiscard]] std::uint64_t mftClusterCount() const noexcept;
	[[nodiscard]] std::uint64_t dataStartCluster() const noexcept;
	[[nodiscard]] std::uint64_t totalBytes() const noexcept;
};

// How many records the fixture volume's `$MFT` holds. Named here because the
// scaled plan below is defined as "this one, with more of them".
inline constexpr std::uint32_t kMftRecordCount = 32;

// The one fixed plan the whole fixture is built from. It takes no arguments on
// purpose: a fixture whose shape can vary is one no test can assert against.
[[nodiscard]] NtfsLayout makeLayout() noexcept;

// The same plan with a larger `$MFT`, for the `ntfs-enumerate` benchmark: a
// volume of seven files is enumerated faster than a process starts, and a rate
// measured over that is a measurement of process startup. The data region
// keeps its size and its contents, so the volume the fixture describes is the
// same volume — with more records behind it. At `kMftRecordCount` it *is*
// `makeLayout()`.
[[nodiscard]] NtfsLayout makeLayoutForRecords(std::uint32_t mftRecordCount) noexcept;

} // namespace revenant::imagegen::ntfs
