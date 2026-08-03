// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/disk/DiskImageBuilder.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

#include "imagegen/ByteWriter.hpp"
#include "imagegen/ImageFile.hpp"
#include "imagegen/exfat/ExfatImageBuilder.hpp"
#include "imagegen/ext4/Ext4ImageBuilder.hpp"
#include "imagegen/fat/Fat32ImageBuilder.hpp"
#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::imagegen::disk {

namespace {

constexpr std::uint64_t kSectorBytes = 512;
// What every partitioner since Vista aligns to, and what makes a partition's
// first sector land on an erase block rather than across two of them.
constexpr std::uint64_t kAlignmentBytes = 1U << 20U;
constexpr std::size_t kTableOffset = 0x1BE;
constexpr std::size_t kEntryBytes = 16;
constexpr std::size_t kSignatureOffset = 0x1FE;
constexpr std::uint16_t kSignature = 0xAA55;
// The one type byte three of these filesystems share: MBR never distinguished
// HPFS, NTFS and exFAT.
constexpr std::uint8_t kIfsType = 0x07;

// One fixture volume and the type byte a formatter would have written for it.
struct Volume {
	std::vector<std::byte> bytes;
	std::uint8_t typeCode = 0;
};

// Where one volume ended up and how many sectors it occupies.
struct Placement {
	std::uint64_t offsetBytes = 0;
	std::uint64_t sectorCount = 0;
	std::uint8_t typeCode = 0;
};

[[nodiscard]] std::vector<Volume> fixtureVolumes() {
	std::vector<Volume> volumes;
	volumes.push_back(Volume{.bytes = ntfs::buildNtfsImage(), .typeCode = kIfsType});
	volumes.push_back(Volume{.bytes = fat::buildFat32Image(), .typeCode = 0x0C});
	volumes.push_back(Volume{.bytes = exfat::buildExfatImage(), .typeCode = kIfsType});
	volumes.push_back(Volume{.bytes = ext4::buildExt4Image(), .typeCode = 0x83});
	return volumes;
}

[[nodiscard]] std::uint64_t alignedUp(std::uint64_t value) {
	return ((value + kAlignmentBytes - 1) / kAlignmentBytes) * kAlignmentBytes;
}

// The first partition starts one alignment unit in, which leaves the table and
// the gap a real disk keeps in front of its first partition.
[[nodiscard]] std::vector<Placement> placementsFor(const std::vector<Volume>& volumes) {
	std::vector<Placement> placements;
	std::uint64_t at = kAlignmentBytes;
	for (const Volume& volume : volumes) {
		const auto sectors = (volume.bytes.size() + kSectorBytes - 1) / kSectorBytes;
		placements.push_back(
			Placement{.offsetBytes = at, .sectorCount = sectors, .typeCode = volume.typeCode});
		at = alignedUp(at + volume.bytes.size());
	}
	return placements;
}

// One slot, written into the table that begins in the sector at `sectorAt`.
// Offsets are stated relative to that sector because an entry's start LBA is:
// a table describes the device whose sector it sits in.
void writeEntry(
	std::vector<std::byte>& disk,
	std::size_t sectorAt,
	std::size_t index,
	const Placement& placement) {
	const auto at = sectorAt + kTableOffset + (index * kEntryBytes);
	putLe<std::uint8_t>(disk, at + 0x04, placement.typeCode);
	putLe<std::uint32_t>(
		disk,
		at + 0x08,
		static_cast<std::uint32_t>(placement.offsetBytes / kSectorBytes));
	putLe<std::uint32_t>(disk, at + 0x0C, static_cast<std::uint32_t>(placement.sectorCount));
}

void writeTable(
	std::vector<std::byte>& disk,
	std::size_t sectorAt,
	const std::vector<Placement>& placements) {
	for (std::size_t index = 0; index < placements.size(); ++index) {
		writeEntry(disk, sectorAt, index, placements.at(index));
	}
	putLe<std::uint16_t>(disk, sectorAt + kSignatureOffset, kSignature);
}

void writeVolumes(
	std::vector<std::byte>& disk,
	const std::vector<Volume>& volumes,
	const std::vector<Placement>& placements) {
	for (std::size_t index = 0; index < volumes.size(); ++index) {
		putBytes(
			disk,
			static_cast<std::size_t>(placements.at(index).offsetBytes),
			volumes.at(index).bytes);
	}
}

[[nodiscard]] std::vector<std::uint64_t> offsetsOf(const std::vector<Placement>& placements) {
	std::vector<std::uint64_t> offsets;
	offsets.reserve(placements.size());
	for (const Placement& placement : placements) {
		offsets.push_back(placement.offsetBytes);
	}
	return offsets;
}

// One alignment unit past the last volume, so the disk ends on the same boundary
// its partitions start on. A disk with no volumes has no size — the fixture
// never asks for one, but `back()` on an empty vector is undefined behaviour and
// GCC 14 says so at `-O2` once this inlines into buildDisk(), where GCC 13 and
// clang do not (story-0606).
[[nodiscard]] std::size_t diskBytesFor(const std::vector<Placement>& placements) {
	if (placements.empty()) {
		return 0;
	}
	const auto last = placements.back();
	return static_cast<std::size_t>(
		alignedUp(last.offsetBytes + (last.sectorCount * kSectorBytes)));
}

// The slot the phantom table carries: one sector into the volume that table
// sits in, running to that volume's end. It is well formed on purpose — a
// window that clamped to nothing would make the fixture a test about damage,
// and what is under test is that nobody looks inside a volume for a table.
[[nodiscard]] Placement phantomIn(const Placement& volume) {
	return Placement{
		.offsetBytes = kSectorBytes,
		.sectorCount = volume.sectorCount - 1,
		.typeCode = kIfsType};
}

// The disk both fixtures are, before either is asked for: the four volumes
// placed, and the table that describes them. The placements come back out
// because the phantom needs to know where the first volume landed.
struct BuiltDisk {
	std::vector<std::byte> bytes;
	std::vector<Placement> placements;
};

[[nodiscard]] BuiltDisk buildDisk() {
	const auto volumes = fixtureVolumes();
	auto placements = placementsFor(volumes);
	std::vector<std::byte> disk(diskBytesFor(placements), std::byte{0});
	writeTable(disk, 0, placements);
	writeVolumes(disk, volumes, placements);
	return BuiltDisk{.bytes = std::move(disk), .placements = std::move(placements)};
}

[[nodiscard]] DiskImage imageOf(BuiltDisk built) {
	return DiskImage{.bytes = std::move(built.bytes), .volumeOffsets = offsetsOf(built.placements)};
}

} // namespace

DiskImage buildMbrDiskImage() {
	return imageOf(buildDisk());
}

DiskImage buildPhantomTableDiskImage() {
	auto built = buildDisk();
	const Placement volume = built.placements.front();
	writeTable(built.bytes, static_cast<std::size_t>(volume.offsetBytes), {phantomIn(volume)});
	return imageOf(std::move(built));
}

Result<std::uint64_t> writeMbrDiskImage(const std::filesystem::path& path) {
	return writeImageBytes(path, buildMbrDiskImage().bytes);
}

} // namespace revenant::imagegen::disk
