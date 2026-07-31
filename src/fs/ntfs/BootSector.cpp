// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ntfs/BootSector.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

#include "BootSectorInternal.hpp"
#include "fs/MountRegion.hpp"
#include "core/SafeArith.hpp"
#include "fs/BpbFields.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ntfs {

namespace {

// Geometry accumulated across the validation chain. Each step below folds one
// validated field in, so a rejection anywhere short-circuits the whole parse.
struct BootState {
	std::uint32_t bytesPerSector = 0;
	std::uint32_t sectorsPerCluster = 0;
	std::uint32_t bytesPerCluster = 0;
	std::uint64_t totalSectors = 0;
	std::uint64_t mftCluster = 0;
	std::uint64_t mftOffsetBytes = 0;
	std::uint32_t bytesPerMftRecord = 0;
};

[[nodiscard]] Result<BootState> withBytesPerSector(const ByteReader& reader, bool /*unused*/) {
	return bytesPerSector(reader).map(
		[](std::uint32_t bps) { return BootState{.bytesPerSector = bps}; });
}

[[nodiscard]] Result<BootState>
withSectorsPerCluster(const ByteReader& reader, const BootState& s) {
	return sectorsPerCluster(reader).map([&](std::uint32_t spc) {
		auto next = s;
		next.sectorsPerCluster = spc;
		return next;
	});
}

[[nodiscard]] Result<BootState> withClusterSize(const ByteReader& /*unused*/, const BootState& s) {
	return safeMul32(s.bytesPerSector, s.sectorsPerCluster, 0x0D).map([&](std::uint32_t bpc) {
		auto next = s;
		next.bytesPerCluster = bpc;
		return next;
	});
}

[[nodiscard]] Result<BootState> withTotalSectors(const ByteReader& reader, const BootState& s) {
	return totalSectors(reader).map([&](std::uint64_t total) {
		auto next = s;
		next.totalSectors = total;
		return next;
	});
}

[[nodiscard]] Result<BootState> withMftCluster(const ByteReader& reader, const BootState& s) {
	return mftClusterNumber(reader).andThen([&](std::uint64_t mft) {
		if (mft >= s.totalSectors / s.sectorsPerCluster) {
			return Result<BootState>(Error{.code = ErrorCode::kInvalidArgument, .offset = 0x30});
		}
		auto next = s;
		next.mftCluster = mft;
		return Result<BootState>(next);
	});
}

[[nodiscard]] Result<BootState> withMftOffset(const ByteReader& /*unused*/, const BootState& s) {
	return safeMul64(s.mftCluster, s.bytesPerCluster, 0x30).map([&](std::uint64_t offset) {
		auto next = s;
		next.mftOffsetBytes = offset;
		return next;
	});
}

[[nodiscard]] Result<BootState> withRecordSize(const ByteReader& reader, const BootState& s) {
	return mftRecordSize(reader, s.bytesPerCluster).map([&](std::uint32_t record) {
		auto next = s;
		next.bytesPerMftRecord = record;
		return next;
	});
}

[[nodiscard]] Result<NtfsGeometry> withSignature(const ByteReader& reader, const BootState& s) {
	return bootSignatureIsValid(reader).map([&](bool) {
		return NtfsGeometry{
			.bytesPerSector = s.bytesPerSector,
			.bytesPerCluster = s.bytesPerCluster,
			.totalClusters = s.totalSectors / s.sectorsPerCluster,
			.mftOffsetBytes = s.mftOffsetBytes,
			.bytesPerMftRecord = s.bytesPerMftRecord};
	});
}

} // namespace

Result<NtfsGeometry> parseBootSector(std::span<const std::byte> sector) {
	if (sector.size() < kBootSectorBytes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = sector.size()};
	}
	const ByteReader reader{sector.first(kBootSectorBytes)};
	return oemIdIsValid(reader)
		.andThen(std::bind_front(withBytesPerSector, reader))
		.andThen(std::bind_front(withSectorsPerCluster, reader))
		.andThen(std::bind_front(withClusterSize, reader))
		.andThen(std::bind_front(withTotalSectors, reader))
		.andThen(std::bind_front(withMftCluster, reader))
		.andThen(std::bind_front(withMftOffset, reader))
		.andThen(std::bind_front(withRecordSize, reader))
		.andThen(std::bind_front(withSignature, reader));
}

} // namespace revenant::fs::ntfs
