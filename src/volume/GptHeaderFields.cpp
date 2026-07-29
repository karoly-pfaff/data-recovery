// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Crc32.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/volume/Gpt.hpp"
#include "volume/GptInternal.hpp"
#include "volume/GptLayout.hpp"

namespace revenant::volume {

namespace {

constexpr std::size_t kCrcFieldBytes = 4;

// Where the header keeps its own checksum, or nothing when the header is too
// short to hold one. Answered by looking rather than by trusting the offset:
// bytes too short to carry a checksum carry no checksum to zero.
[[nodiscard]] std::span<std::byte> checksumField(std::span<std::byte> header) {
	if (header.size() < kHeaderCrcOffset + kCrcFieldBytes) {
		return {};
	}
	return header.subspan(kHeaderCrcOffset, kCrcFieldBytes);
}

// The header with its own checksum field taken as zero — the bytes the value it
// carries was computed over.
[[nodiscard]] std::vector<std::byte> withoutChecksum(std::span<const std::byte> header) {
	std::vector<std::byte> bytes{header.begin(), header.end()};
	std::ranges::fill(checksumField(bytes), std::byte{0});
	return bytes;
}

// The usable range, checked as a range: a first sector above the last one
// describes no disk at all.
[[nodiscard]] Result<GptHeader> withUsableRange(const ByteReader& reader, GptHeader header) {
	return reader.readLe<std::uint64_t>(kFirstUsableOffset).andThen([&](std::uint64_t first) {
		return reader.readLe<std::uint64_t>(kLastUsableOffset).andThen([&](std::uint64_t last) {
			if (first > last) {
				return Result<GptHeader>(
					Error{.code = ErrorCode::kInvalidArgument, .offset = kFirstUsableOffset});
			}
			header.firstUsableLba = first;
			header.lastUsableLba = last;
			return Result<GptHeader>(header);
		});
	});
}

// An entry's size is fixed by the specification at a multiple of eight, no
// smaller than 128. A size that breaks either rule cannot be walked, whatever
// the checksum says about it.
[[nodiscard]] Result<std::uint32_t> entrySizeIn(const ByteReader& reader) {
	return reader.readLe<std::uint32_t>(kEntryBytesOffset).andThen([](std::uint32_t size) {
		if (size < kGptEntryBytes || size % kEntrySizeGranularity != 0) {
			return Result<std::uint32_t>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = kEntryBytesOffset});
		}
		return Result<std::uint32_t>(size);
	});
}

// How many entries the array holds, bounded by what the whole array may weigh
// (ADR-0009): the count and the size are both attacker-chosen, and their product
// would size the allocation that reads it.
[[nodiscard]] Result<std::uint32_t>
entryCountIn(const ByteReader& reader, std::uint32_t entryBytes) {
	return reader.readLe<std::uint32_t>(kEntryCountOffset)
		.andThen([entryBytes](std::uint32_t count) {
			if (static_cast<std::uint64_t>(count) * entryBytes > kMaxEntryArrayBytes) {
				return Result<std::uint32_t>(
					Error{.code = ErrorCode::kOutOfRange, .offset = kEntryCountOffset});
			}
			return Result<std::uint32_t>(count);
		});
}

[[nodiscard]] Result<GptHeader> withEntryArray(const ByteReader& reader, GptHeader header) {
	return entrySizeIn(reader).andThen([&](std::uint32_t entryBytes) {
		return entryCountIn(reader, entryBytes).map([&](std::uint32_t count) {
			header.entryBytes = entryBytes;
			header.entryCount = count;
			return header;
		});
	});
}

[[nodiscard]] Result<GptHeader> withPlacement(const ByteReader& reader, GptHeader header) {
	return reader.readLe<std::uint64_t>(kMyLbaOffset).andThen([&](std::uint64_t mine) {
		return reader.readLe<std::uint64_t>(kAlternateLbaOffset).map([&](std::uint64_t alternate) {
			header.myLba = mine;
			header.alternateLba = alternate;
			return header;
		});
	});
}

[[nodiscard]] Result<GptHeader> withArrayLocation(const ByteReader& reader, GptHeader header) {
	return reader.readLe<std::uint64_t>(kEntryArrayLbaOffset).andThen([&](std::uint64_t lba) {
		return reader.readLe<std::uint32_t>(kEntryArrayCrcOffset).map([&](std::uint32_t crc) {
			header.entryArrayLba = lba;
			header.entryArrayCrc = crc;
			return header;
		});
	});
}

} // namespace

Result<bool> namesGpt(const ByteReader& reader) {
	return reader.bytes(kSignatureOffset, kGptSignature.size())
		.andThen([](std::span<const std::byte> raw) {
			if (!std::ranges::equal(raw, kGptSignature)) {
				return Result<bool>(
					Error{.code = ErrorCode::kInvalidArgument, .offset = kSignatureOffset});
			}
			return Result<bool>(true);
		});
}

Result<std::size_t> headerSizeIn(const ByteReader& reader, std::size_t available) {
	return reader.readLe<std::uint32_t>(kHeaderSizeOffset).andThen([available](std::uint32_t size) {
		if (size < kGptHeaderBytes || size > available) {
			return Result<std::size_t>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = kHeaderSizeOffset});
		}
		return Result<std::size_t>(static_cast<std::size_t>(size));
	});
}

Result<bool> checksumIsValid(std::span<const std::byte> header) {
	const ByteReader reader{header};
	return reader.readLe<std::uint32_t>(kHeaderCrcOffset).andThen([header](std::uint32_t stated) {
		if (crc32(withoutChecksum(header)) != stated) {
			return Result<bool>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = kHeaderCrcOffset});
		}
		return Result<bool>(true);
	});
}

Result<bool> placedAt(const ByteReader& reader, std::uint64_t atLba) {
	return reader.readLe<std::uint64_t>(kMyLbaOffset).andThen([atLba](std::uint64_t mine) {
		if (mine != atLba) {
			return Result<bool>(Error{.code = ErrorCode::kInvalidArgument, .offset = kMyLbaOffset});
		}
		return Result<bool>(true);
	});
}

Result<GptHeader> headerBodyOf(const ByteReader& reader) {
	return withPlacement(reader, GptHeader{})
		.andThen([&](const GptHeader& header) { return withUsableRange(reader, header); })
		.andThen([&](const GptHeader& header) { return withArrayLocation(reader, header); })
		.andThen([&](const GptHeader& header) { return withEntryArray(reader, header); });
}

} // namespace revenant::volume
