// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/NtfsVolume.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/MftTable.hpp"
#include "support/InMemoryDevice.hpp"

namespace revenant::testing {

namespace {

[[nodiscard]] std::vector<std::byte>::iterator
at(std::vector<std::byte>& image, std::uint64_t offset) {
	return image.begin() + static_cast<std::vector<std::byte>::difference_type>(offset);
}

} // namespace

std::uint64_t recordOffset(std::uint64_t number) noexcept {
	const auto layout = imagegen::ntfs::makeLayout();
	return layout.mftOffsetBytes() + (number * layout.mftRecordBytes);
}

NtfsVolume::NtfsVolume() : image_(imagegen::ntfs::buildNtfsImage()) {}

std::span<const std::byte> NtfsVolume::bytes() const noexcept {
	return image_;
}

void NtfsVolume::putRecord(std::uint64_t number, std::span<const std::byte> record) {
	std::ranges::copy(record, at(image_, recordOffset(number)));
}

void NtfsVolume::copyWithin(VolumeRange source, std::uint64_t toOffset) {
	const std::vector<std::byte> copied(
		at(image_, source.offset),
		at(image_, source.offset) + static_cast<std::ptrdiff_t>(source.length));
	std::ranges::copy(copied, at(image_, toOffset));
}

void NtfsVolume::clear(VolumeRange range) {
	std::fill_n(at(image_, range.offset), range.length, std::byte{0});
}

fs::ntfs::NtfsGeometry NtfsVolume::geometry() const {
	const auto sector = std::span{image_}.first(imagegen::ntfs::makeLayout().bytesPerSector);
	return fs::ntfs::parseBootSector(sector).value();
}

BlockDevice& NtfsVolume::mount() {
	device_ = std::make_unique<InMemoryDevice>(image_, imagegen::ntfs::makeLayout().bytesPerSector);
	return *device_;
}

Result<fs::ntfs::MftTable> NtfsVolume::openTable() {
	return fs::ntfs::MftTable::open(mount(), geometry());
}

} // namespace revenant::testing
