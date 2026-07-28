// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ntfs/AttributeBuilder.hpp"

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

#include "imagegen/ByteWriter.hpp"
#include "imagegen/ntfs/AttributeInternal.hpp"
#include "imagegen/ntfs/RunlistEncoder.hpp"
#include "revenant/fs/ntfs/Runlist.hpp"

namespace revenant::imagegen::ntfs {

namespace {

constexpr std::size_t kAlignment = 8;
constexpr std::size_t kResidentHeaderBytes = 0x18;
constexpr std::size_t kNonResidentHeaderBytes = 0x40;
constexpr std::uint32_t kEndMarkerType = 0xFFFFFFFFU;

// Header field positions, shared by both attribute forms.
constexpr std::size_t kTypeOffset = 0x00;
constexpr std::size_t kLengthOffset = 0x04;
constexpr std::size_t kNonResidentFlagOffset = 0x08;
constexpr std::size_t kContentLengthOffset = 0x10;
constexpr std::size_t kContentOffsetOffset = 0x14;
constexpr std::size_t kLastVcnOffset = 0x18;
constexpr std::size_t kRunlistOffsetOffset = 0x20;
constexpr std::size_t kAllocatedSizeOffset = 0x28;
constexpr std::size_t kRealSizeOffset = 0x30;
constexpr std::size_t kInitializedSizeOffset = 0x38;

[[nodiscard]] std::size_t alignUp(std::size_t value) noexcept {
	return ((value + kAlignment - 1) / kAlignment) * kAlignment;
}

[[nodiscard]] std::uint64_t totalClusters(std::span<const revenant::fs::ntfs::DataRun> runs) {
	return std::accumulate(
		runs.begin(),
		runs.end(),
		std::uint64_t{0},
		[](std::uint64_t sum, const revenant::fs::ntfs::DataRun& run) {
			return sum + run.lengthClusters;
		});
}

// The non-resident header's size trio: what the runs allocate, what the file
// claims, and how much of it is actually written (all of it, here).
void putSizes(std::vector<std::byte>& attribute, const NonResidentDataSpec& spec) {
	const auto allocated = totalClusters(spec.runs) * spec.bytesPerCluster;
	putLe<std::uint64_t>(attribute, kAllocatedSizeOffset, allocated);
	putLe<std::uint64_t>(attribute, kRealSizeOffset, spec.realSize);
	putLe<std::uint64_t>(attribute, kInitializedSizeOffset, spec.realSize);
}

void putNonResidentHeader(
	std::vector<std::byte>& attribute,
	const NonResidentDataSpec& spec,
	std::size_t length) {
	putLe<std::uint32_t>(attribute, kTypeOffset, kDataType);
	putLe<std::uint32_t>(attribute, kLengthOffset, static_cast<std::uint32_t>(length));
	putLe<std::uint8_t>(attribute, kNonResidentFlagOffset, 1);
	putLe<std::uint64_t>(attribute, kLastVcnOffset, totalClusters(spec.runs) - 1);
	putLe<std::uint16_t>(attribute, kRunlistOffsetOffset, kNonResidentHeaderBytes);
	putSizes(attribute, spec);
}

} // namespace

std::vector<std::byte> residentAttribute(std::uint32_t type, std::span<const std::byte> content) {
	const auto length = alignUp(kResidentHeaderBytes + content.size());
	std::vector<std::byte> attribute(length, std::byte{0});
	putLe<std::uint32_t>(attribute, kTypeOffset, type);
	putLe<std::uint32_t>(attribute, kLengthOffset, static_cast<std::uint32_t>(length));
	putLe<std::uint32_t>(
		attribute,
		kContentLengthOffset,
		static_cast<std::uint32_t>(content.size()));
	putLe<std::uint16_t>(attribute, kContentOffsetOffset, kResidentHeaderBytes);
	putBytes(attribute, kResidentHeaderBytes, content);
	return attribute;
}

std::vector<std::byte> buildResidentData(std::span<const std::byte> content) {
	return residentAttribute(kDataType, content);
}

std::vector<std::byte> buildNonResidentData(const NonResidentDataSpec& spec) {
	const auto runlist = encodeRunlist(spec.runs);
	const auto length = alignUp(kNonResidentHeaderBytes + runlist.size());
	std::vector<std::byte> attribute(length, std::byte{0});
	putNonResidentHeader(attribute, spec, length);
	putBytes(attribute, kNonResidentHeaderBytes, runlist);
	return attribute;
}

std::vector<std::byte> buildEndMarker() {
	std::vector<std::byte> marker(sizeof(kEndMarkerType), std::byte{0});
	putLe<std::uint32_t>(marker, kTypeOffset, kEndMarkerType);
	return marker;
}

} // namespace revenant::imagegen::ntfs
