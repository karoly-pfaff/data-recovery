// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ntfs/NtfsFileSystem.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <utility>

#include "BootSectorInternal.hpp"
#include "fs/MountRegion.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/EntryEnumeration.hpp"
#include "revenant/fs/ntfs/MftTable.hpp"

namespace revenant::fs::ntfs {

namespace {

// The mounted volume: an `$MFT` addressed once, walked as often as it is asked.
class NtfsFileSystem final : public FileSystem {
public:
	explicit NtfsFileSystem(MftTable table) noexcept : table_(std::move(table)) {}

	[[nodiscard]] Result<EnumerationStats> enumerate(EntryVisitor& visitor) const override {
		return enumerateEntries(table_, visitor);
	}

private:
	MftTable table_;
};

// NTFS names itself in the OEM id. Anything else is another filesystem's
// volume rather than a broken NTFS one, and is handed back to the mount table
// to keep looking.
[[nodiscard]] Result<NtfsGeometry> recognize(std::span<const std::byte> sector) {
	if (!oemIdIsValid(ByteReader{sector}).hasValue()) {
		return Error{.code = ErrorCode::kNotFound};
	}
	return parseBootSector(sector);
}

[[nodiscard]] Result<std::unique_ptr<FileSystem>> mountedOn(const MftTable& table) {
	return std::unique_ptr<FileSystem>{std::make_unique<NtfsFileSystem>(table)};
}

} // namespace

Result<std::unique_ptr<FileSystem>> mountNtfs(BlockDevice& device) {
	return readMountRegion(device, MountRegion{.offset = 0, .length = kBootSectorBytes})
		.andThen(recognize)
		.andThen(
			[&device](const NtfsGeometry& geometry) { return MftTable::open(device, geometry); })
		.andThen(mountedOn);
}

} // namespace revenant::fs::ntfs
