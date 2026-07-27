// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ntfs/BootSector.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ntfs {

namespace {

constexpr std::size_t kBootSectorSize = 512;
constexpr std::array<std::byte, 8> kOemId{
	std::byte{'N'},
	std::byte{'T'},
	std::byte{'F'},
	std::byte{'S'},
	std::byte{' '},
	std::byte{' '},
	std::byte{' '},
	std::byte{' '}};

[[nodiscard]] std::int8_t toSigned(std::uint8_t value) noexcept {
	return std::bit_cast<std::int8_t>(value);
}

[[nodiscard]] bool isPowerOfTwo(std::uint32_t value) noexcept {
	return value != 0 && (value & (value - 1)) == 0;
}

// The first two parameters are multiplied; the third is a diagnostic offset.
// They are always passed by name at call sites, so the swap risk does not apply.
[[nodiscard]] Result<std::uint32_t>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
safeMul32(std::uint32_t a, std::uint32_t b, std::uint64_t offset) noexcept {
	const auto product = static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
	if (product > std::numeric_limits<std::uint32_t>::max()) {
		return Error{.code = ErrorCode::kOverflow, .offset = offset};
	}
	return static_cast<std::uint32_t>(product);
}

[[nodiscard]] Result<std::uint64_t>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
safeMul64(std::uint64_t a, std::uint64_t b, std::uint64_t offset) noexcept {
	if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
		return Error{.code = ErrorCode::kOverflow, .offset = offset};
	}
	return a * b;
}

[[nodiscard]] Result<bool> oemIdIsValid(const ByteReader& reader) {
	return reader.bytes(0x03, 8).andThen([](std::span<const std::byte> raw) {
		if (!std::ranges::equal(raw, kOemId)) {
			return Result<bool>(Error{.code = ErrorCode::kInvalidArgument, .offset = 0x03});
		}
		return Result<bool>(true);
	});
}

[[nodiscard]] Result<std::uint32_t> bytesPerSector(const ByteReader& reader) {
	return reader.readLe<std::uint16_t>(0x0B).andThen([](std::uint16_t raw) {
		const auto value = static_cast<std::uint32_t>(raw);
		if (value != 512U && value != 1024U && value != 2048U && value != 4096U) {
			return Result<std::uint32_t>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = 0x0B});
		}
		return Result<std::uint32_t>(value);
	});
}

[[nodiscard]] Result<std::uint32_t> sectorsPerCluster(const ByteReader& reader) {
	return reader.bytes(0x0D, 1).andThen([](std::span<const std::byte> raw) {
		const auto value = std::to_integer<std::uint32_t>(raw.front());
		if (!isPowerOfTwo(value) || value > 128U) {
			return Result<std::uint32_t>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = 0x0D});
		}
		return Result<std::uint32_t>(value);
	});
}

[[nodiscard]] Result<std::uint64_t> totalSectors(const ByteReader& reader) {
	return reader.readLe<std::uint64_t>(0x28).andThen([](std::uint64_t raw) {
		if (raw == 0U) {
			return Result<std::uint64_t>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = 0x28});
		}
		return Result<std::uint64_t>(raw);
	});
}

[[nodiscard]] Result<std::uint64_t> mftClusterNumber(const ByteReader& reader) {
	return reader.readLe<std::uint64_t>(0x30);
}

[[nodiscard]] Result<std::uint32_t>
recordSizeFromClusters(std::int8_t value, std::uint32_t bytesPerCluster) {
	return safeMul32(static_cast<std::uint32_t>(value), bytesPerCluster, 0x40)
		.andThen([&](std::uint32_t recordBytes) {
			if (recordBytes < 256U || recordBytes > 65536U) {
				return Result<std::uint32_t>(
					Error{.code = ErrorCode::kInvalidArgument, .offset = 0x40});
			}
			return Result<std::uint32_t>(recordBytes);
		});
}

[[nodiscard]] Result<std::uint32_t> recordSizeFromLog2(std::int8_t value) {
	const auto shift = -value;
	if (shift < 8 || shift > 16) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = 0x40};
	}
	return std::uint32_t{1} << static_cast<std::uint32_t>(shift);
}

[[nodiscard]] Result<std::uint32_t>
recordSizeFromRaw(std::int8_t value, std::uint32_t bytesPerCluster) {
	if (value > 0) {
		return recordSizeFromClusters(value, bytesPerCluster);
	}
	if (value < 0) {
		return recordSizeFromLog2(value);
	}
	return Error{.code = ErrorCode::kInvalidArgument, .offset = 0x40};
}

[[nodiscard]] Result<std::uint32_t>
mftRecordSize(const ByteReader& reader, std::uint32_t bytesPerCluster) {
	return reader.bytes(0x40, 1).andThen([&](std::span<const std::byte> raw) {
		const auto value = toSigned(std::to_integer<std::uint8_t>(raw.front()));
		return recordSizeFromRaw(value, bytesPerCluster);
	});
}

[[nodiscard]] Result<bool> signatureIsValid(const ByteReader& reader) {
	return reader.bytes(0x1FE, 2).andThen([](std::span<const std::byte> raw) {
		if (raw.front() != std::byte{0x55} || raw.back() != std::byte{0xAA}) {
			return Result<bool>(Error{.code = ErrorCode::kInvalidArgument, .offset = 0x1FE});
		}
		return Result<bool>(true);
	});
}

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
	return signatureIsValid(reader).map([&](bool) {
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
	if (sector.size() < kBootSectorSize) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = sector.size()};
	}
	const ByteReader reader{sector.first(kBootSectorSize)};
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
