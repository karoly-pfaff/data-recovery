// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ntfs/RunlistEncoder.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/fs/ntfs/Runlist.hpp"

namespace revenant::imagegen::ntfs {

namespace {

using revenant::fs::ntfs::DataRun;

constexpr std::size_t kBitsPerByte = 8;
constexpr std::size_t kMaxFieldWidth = 8;
constexpr std::size_t kNibbleBits = 4;
constexpr std::uint64_t kByteMask = 0xFFU;

// One on-disk field: the value and how many bytes it is written in.
struct Field {
	std::uint64_t value;
	std::size_t width;
};

// The two fields of one run, already reduced to what goes on disk.
struct RunFields {
	std::uint64_t lengthClusters;
	std::int64_t delta;
	std::size_t lengthWidth;
	std::size_t offsetWidth; // 0 marks a sparse run: no offset field at all
};

// Narrowest width that holds `value` unsigned; the length field is never signed.
[[nodiscard]] std::size_t unsignedWidth(std::uint64_t value) noexcept {
	std::size_t width = 1;
	while (width < kMaxFieldWidth && (value >> (width * kBitsPerByte)) != 0) {
		++width;
	}
	return width;
}

// Narrowest width that holds `value` in two's complement with its sign intact.
// The loop stops before width 8, so the shift below never reaches 63.
[[nodiscard]] std::size_t signedWidth(std::int64_t value) noexcept {
	std::size_t width = 1;
	while (width < kMaxFieldWidth) {
		const std::int64_t limit = std::int64_t{1} << ((width * kBitsPerByte) - 1);
		if (value >= -limit && value < limit) {
			break;
		}
		++width;
	}
	return width;
}

[[nodiscard]] RunFields fieldsFor(const DataRun& run, std::int64_t previousLcn) noexcept {
	const auto delta = static_cast<std::int64_t>(run.startCluster) - previousLcn;
	return RunFields{
		.lengthClusters = run.lengthClusters,
		.delta = delta,
		.lengthWidth = unsignedWidth(run.lengthClusters),
		.offsetWidth = run.sparse ? 0 : signedWidth(delta)};
}

void appendLe(std::vector<std::byte>& out, const Field& field) {
	for (std::size_t i = 0; i < field.width; ++i) {
		out.push_back(static_cast<std::byte>((field.value >> (i * kBitsPerByte)) & kByteMask));
	}
}

// The header byte packs the offset width in the high nibble and the length
// width in the low one.
void appendRun(std::vector<std::byte>& out, const RunFields& fields) {
	out.push_back(static_cast<std::byte>((fields.offsetWidth << kNibbleBits) | fields.lengthWidth));
	appendLe(out, Field{.value = fields.lengthClusters, .width = fields.lengthWidth});
	appendLe(
		out,
		Field{.value = static_cast<std::uint64_t>(fields.delta), .width = fields.offsetWidth});
}

// A hole is backed by no clusters, so it leaves the delta origin untouched.
[[nodiscard]] std::int64_t lcnAfter(const DataRun& run, std::int64_t previousLcn) noexcept {
	return run.sparse ? previousLcn : static_cast<std::int64_t>(run.startCluster);
}

} // namespace

std::vector<std::byte> encodeRunlist(std::span<const DataRun> runs) {
	std::vector<std::byte> out;
	std::int64_t previousLcn = 0;
	for (const auto& run : runs) {
		appendRun(out, fieldsFor(run, previousLcn));
		previousLcn = lcnAfter(run, previousLcn);
	}
	out.push_back(std::byte{0x00});
	return out;
}

} // namespace revenant::imagegen::ntfs
