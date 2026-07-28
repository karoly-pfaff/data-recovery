// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/exfat/ExfatFixture.hpp"

#include <cstddef>
#include <vector>

#include "imagegen/FixtureBytes.hpp"
#include "imagegen/exfat/ExfatLayout.hpp"

namespace revenant::imagegen::exfat {

std::vector<ExfatFile> rootFiles() {
	std::vector<ExfatFile> files;
	files.push_back(
		ExfatFile{
			.name = "keep.txt",
			.clusters = {kKeepCluster, kKeepSecondCluster},
			.live = true,
			.contiguous = false,
			.content = fixtureContent(5000, std::byte{1})});
	files.push_back(
		ExfatFile{
			.name = "gone.txt",
			.clusters = {kDeletedCluster},
			.live = false,
			.contiguous = true,
			.content = fixtureContent(3000, std::byte{2})});
	files.push_back(
		ExfatFile{
			.name = "wiped.txt",
			.clusters = {kOverwrittenCluster},
			.live = false,
			.contiguous = true,
			.content = fixtureContent(1000, std::byte{3})});
	return files;
}

std::vector<ExfatFile> photosFiles() {
	std::vector<ExfatFile> files;
	files.push_back(
		ExfatFile{
			.name = "inner.bin",
			.clusters = {kInnerCluster},
			.live = true,
			.contiguous = true,
			.content = fixtureContent(2000, std::byte{4})});
	return files;
}

} // namespace revenant::imagegen::exfat
