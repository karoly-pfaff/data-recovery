// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>

#include "BootSectorInternal.hpp"
#include "fs/BpbFields.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/SafeArith.hpp"
#include "revenant/fs/fat/BootSector.hpp"

namespace revenant::fs::fat {

namespace {

// Where the data region starts and how much of it there is, still in sectors.
struct Placement {
	std::uint64_t dataStartSector;
	std::uint64_t totalClusters;
	std::uint32_t rootCluster;
};

// The same volume restated in bytes, which is the only form a device read can
// use.
struct ByteLayout {
	std::uint64_t fatOffset;
	std::uint64_t fatSize;
	std::uint64_t dataOffset;
};

// Past the reserved region and past every FAT — that is where files begin.
[[nodiscard]] Result<std::uint64_t> dataStartSector(const Bpb& bpb) {
	return safeMul64(bpb.fatCount, bpb.fatSectors, kFatSizeOffset)
		.andThen([&](std::uint64_t allFats) {
			return safeAdd64(bpb.reservedSectors, allFats, kFatSizeOffset);
		});
}

// How many whole clusters fit after that. A volume whose bookkeeping leaves no
// room for a single cluster holds no files, whatever its BPB claims.
[[nodiscard]] Result<std::uint64_t> dataClusters(const Bpb& bpb, std::uint64_t dataStart) {
	if (dataStart >= bpb.totalSectors) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kTotalSectorsOffset};
	}
	const auto clusters = (bpb.totalSectors - dataStart) / bpb.sectorsPerCluster;
	if (clusters == 0U) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kTotalSectorsOffset};
	}
	return clusters;
}

// The root directory has to start at a cluster the data region actually holds.
[[nodiscard]] Result<std::uint32_t> checkedRoot(std::uint32_t root, std::uint64_t clusters) {
	if (root < kFirstDataCluster || root >= clusters + kFirstDataCluster) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kRootClusterOffset};
	}
	return root;
}

[[nodiscard]] Result<Placement> placementOf(const Bpb& bpb) {
	return dataStartSector(bpb).andThen([&](std::uint64_t dataStart) {
		return dataClusters(bpb, dataStart).andThen([&](std::uint64_t clusters) {
			return checkedRoot(bpb.rootCluster, clusters).map([&](std::uint32_t root) {
				return Placement{
					.dataStartSector = dataStart,
					.totalClusters = clusters,
					.rootCluster = root};
			});
		});
	});
}

// Where the FAT region begins and how long one FAT is, in bytes.
struct FatBytes {
	std::uint64_t offset;
	std::uint64_t size;
};

[[nodiscard]] Result<FatBytes> fatBytes(const Bpb& bpb) {
	return safeMul64(bpb.reservedSectors, bpb.bytesPerSector, kReservedSectorsOffset)
		.andThen([&](std::uint64_t offset) {
			return safeMul64(bpb.fatSectors, bpb.bytesPerSector, kFatSizeOffset)
				.map([&](std::uint64_t size) { return FatBytes{.offset = offset, .size = size}; });
		});
}

[[nodiscard]] Result<ByteLayout> byteLayout(const Bpb& bpb, std::uint64_t dataStart) {
	return fatBytes(bpb).andThen([&](const FatBytes& fats) {
		return safeMul64(dataStart, bpb.bytesPerSector, kTotalSectorsOffset)
			.map([&](std::uint64_t dataOffset) {
				return ByteLayout{
					.fatOffset = fats.offset,
					.fatSize = fats.size,
					.dataOffset = dataOffset};
			});
	});
}

[[nodiscard]] Fat32Geometry assemble(
	const Bpb& bpb,
	const Placement& placement,
	const ByteLayout& layout,
	std::uint32_t clusterBytes) {
	return Fat32Geometry{
		.bytesPerSector = bpb.bytesPerSector,
		.bytesPerCluster = clusterBytes,
		.fatCount = bpb.fatCount,
		.fatOffsetBytes = layout.fatOffset,
		.fatSizeBytes = layout.fatSize,
		.dataOffsetBytes = layout.dataOffset,
		.totalClusters = placement.totalClusters,
		.rootCluster = placement.rootCluster,
		.belowClusterMinimum = placement.totalClusters < kFat32MinimumClusters};
}

[[nodiscard]] Result<Fat32Geometry> geometryAt(const Bpb& bpb, const Placement& placement) {
	return byteLayout(bpb, placement.dataStartSector).andThen([&](const ByteLayout& layout) {
		return safeMul32(bpb.bytesPerSector, bpb.sectorsPerCluster, kSectorsPerClusterOffset)
			.map([&](std::uint32_t clusterBytes) {
				return assemble(bpb, placement, layout, clusterBytes);
			});
	});
}

} // namespace

Result<Fat32Geometry> geometryFrom(const Bpb& bpb) {
	return placementOf(bpb).andThen(
		[&](const Placement& placement) { return geometryAt(bpb, placement); });
}

} // namespace revenant::fs::fat
