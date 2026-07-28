// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/MftTable.hpp"
#include "support/InMemoryDevice.hpp"

namespace revenant::testing {

// The story-0065 fixture volume, held as editable bytes and mountable as a
// device. A test that needs a *broken* volume states the damage as an edit to
// the known-good image rather than by growing a second image builder, so what
// is under test stays one visible line.
// A byte range of the volume.
struct VolumeRange {
	std::uint64_t offset;
	std::size_t length;
};

// Byte offset of MFT record `number` within the fixture volume.
[[nodiscard]] std::uint64_t recordOffset(std::uint64_t number) noexcept;

class NtfsVolume {
public:
	NtfsVolume();

	[[nodiscard]] std::span<const std::byte> bytes() const noexcept;

	// Overwrites one record slot with `record` (one record's worth of bytes).
	void putRecord(std::uint64_t number, std::span<const std::byte> record);

	// Copies one range of the volume over another offset — how a relocated
	// `$MFT` fragment is planted.
	void copyWithin(VolumeRange source, std::uint64_t toOffset);

	// Zeroes a range, so what used to be there cannot answer a read by accident.
	void clear(VolumeRange range);

	[[nodiscard]] fs::ntfs::NtfsGeometry geometry() const;

	// Mounts the bytes as they stand now; later edits do not reach the device.
	[[nodiscard]] BlockDevice& mount();

	[[nodiscard]] Result<fs::ntfs::MftTable> openTable();

private:
	std::vector<std::byte> image_;
	std::unique_ptr<InMemoryDevice> device_;
};

} // namespace revenant::testing
