// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/fat/Fat32FileSystem.hpp"

#include <cstddef>
#include <memory>
#include <span>

#include "BootSectorInternal.hpp"
#include "fs/MountRegion.hpp"
#include "fs/fat/DirectoryWalk.hpp"
#include "fs/fat/FatTable.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/fat/BootSector.hpp"

namespace revenant::fs::fat {

namespace {

constexpr std::size_t kBootSectorBytes = 512;

// The mounted volume: a FAT located once, and a directory tree walked as often
// as it is asked.
class Fat32FileSystem final : public FileSystem {
public:
	explicit Fat32FileSystem(FatTable table) noexcept : table_(table) {}

	[[nodiscard]] Result<EnumerationStats> enumerate(EntryVisitor& visitor) const override {
		return walkVolume(table_, visitor);
	}

private:
	FatTable table_;
};

// FAT32 names itself in `BS_FilSysType`. Anything else is another filesystem's
// volume rather than a broken FAT32 one, and is handed back to the mount table
// to keep looking.
[[nodiscard]] Result<Fat32Geometry> recognize(std::span<const std::byte> sector) {
	if (!filSysTypeIsFat32(ByteReader{sector}).hasValue()) {
		return Error{.code = ErrorCode::kNotFound};
	}
	return parseFat32BootSector(sector);
}

} // namespace

Result<std::unique_ptr<FileSystem>> mountFat32(BlockDevice& device) {
	return readVolumeStart(device, kBootSectorBytes)
		.andThen(recognize)
		.andThen([&device](const Fat32Geometry& geometry) -> Result<std::unique_ptr<FileSystem>> {
			return std::unique_ptr<FileSystem>{
				std::make_unique<Fat32FileSystem>(FatTable{device, geometry})};
		});
}

} // namespace revenant::fs::fat
