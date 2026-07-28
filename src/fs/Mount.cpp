// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/Mount.hpp"

#include <array>
#include <memory>

#include "fs/ntfs/NtfsFileSystem.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"

namespace revenant::fs {

namespace {

using Mounter = Result<std::unique_ptr<FileSystem>> (*)(BlockDevice&);

// The filesystems this build can read, in probe order. Fixed at link time
// rather than registered into at runtime: the set is known when the binary is
// built, exactly as `builtinCarvers` is. Order is a correctness property and
// belongs in one place — an exFAT volume also carries a FAT-shaped BPB, so
// exFAT is asked before FAT32.
constexpr std::array<Mounter, 1> kMounters{&ntfs::mountNtfs};

// A mounter that did not find its own signature says so with kNotFound, which
// is the one failure that means "ask the next one" rather than "here is your
// answer".
[[nodiscard]] bool declined(const Result<std::unique_ptr<FileSystem>>& mounted) {
	return !mounted.hasValue() && mounted.error().code == ErrorCode::kNotFound;
}

} // namespace

Result<std::unique_ptr<FileSystem>> mountVolume(BlockDevice& device) {
	for (const Mounter mounter : kMounters) {
		auto mounted = mounter(device);
		if (!declined(mounted)) {
			return mounted;
		}
	}
	return Error{.code = ErrorCode::kNotFound};
}

} // namespace revenant::fs
