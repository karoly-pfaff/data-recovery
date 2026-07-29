// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A volume of the fixture's shape with nothing on it but what a test puts
// there, already parsed far enough for one piece of the ext4 reader to be
// reached directly. Built from the same writers `tools/imagegen/ext4` builds the
// fixture image from, so a test never restates a byte offset the builder knows.

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "fs/ext4/BlockReader.hpp"
#include "fs/ext4/InodeTable.hpp"
#include "imagegen/ext4/Ext4Layout.hpp"
#include "imagegen/ext4/Ext4Metadata.hpp"
#include "revenant/fs/ext4/Superblock.hpp"
#include "support/InMemoryDevice.hpp"

namespace revenant::testing {

class Ext4TestVolume {
public:
	explicit Ext4TestVolume(std::vector<std::byte> image)
		: image_(std::move(image)), device_(image_, kSectorSize), geometry_(parsedGeometry()),
		  blocks_(device_, geometry_), inodes_(blocks_) {}

	[[nodiscard]] const fs::ext4::Ext4Blocks& blocks() const noexcept {
		return blocks_;
	}

	[[nodiscard]] const fs::ext4::Ext4InodeTable& inodes() const noexcept {
		return inodes_;
	}

private:
	static constexpr std::uint32_t kSectorSize = 512;

	// The volume describes itself, so the geometry a test reads it through is the
	// one the parser derives rather than one the test asserts into being.
	[[nodiscard]] fs::ext4::Ext4Geometry parsedGeometry() const {
		const auto at = static_cast<std::size_t>(fs::ext4::kSuperblockOffset);
		const auto parsed = fs::ext4::parseExt4Superblock(
			std::span{image_}.subspan(at, fs::ext4::kSuperblockBytes));
		return parsed.hasValue() ? parsed.value() : fs::ext4::Ext4Geometry{};
	}

	std::vector<std::byte> image_;
	InMemoryDevice device_;
	fs::ext4::Ext4Geometry geometry_;
	fs::ext4::Ext4Blocks blocks_;
	fs::ext4::Ext4InodeTable inodes_;
};

// An empty volume of the fixture's shape, ready for a test to write into before
// it is mounted.
[[nodiscard]] inline std::vector<std::byte> emptyExt4Image() {
	return imagegen::ext4::emptyExt4Volume(imagegen::ext4::makeExt4Layout());
}

} // namespace revenant::testing
