// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>

#include "imagegen/CliMain.hpp"
#include "imagegen/disk/DiskImageBuilder.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"

namespace {

// .at() (not operator[]) so bounds are checked even though every index here is
// a compile-time-fixed, in-range constant.
bool run(std::array<std::string, 5>& args) {
	std::array<char*, 5> argv{
		args.at(0).data(),
		args.at(1).data(),
		args.at(2).data(),
		args.at(3).data(),
		args.at(4).data()};
	return revenant::imagegen::runCli(argv);
}

bool runSized(std::array<std::string, 4>& args) {
	std::array<char*, 4> argv{
		args.at(0).data(),
		args.at(1).data(),
		args.at(2).data(),
		args.at(3).data()};
	return revenant::imagegen::runCli(argv);
}

bool runNamed(std::array<std::string, 3>& args) {
	std::array<char*, 3> argv{args.at(0).data(), args.at(1).data(), args.at(2).data()};
	return revenant::imagegen::runCli(argv);
}

[[nodiscard]] std::string tempImage(const std::string& name) {
	return (std::filesystem::temp_directory_path() / name).string();
}

TEST(ImagegenCli, GeneratesPatternImageFromValidArguments) {
	const auto path = tempImage("revenant-cli-test.img");
	std::array<std::string, 5> args{"revenant-imagegen", "pattern", path, "1024", "lba"};
	EXPECT_TRUE(run(args));
	EXPECT_EQ(std::filesystem::file_size(path), 1024U);
	std::filesystem::remove(path);
}

TEST(ImagegenCli, RejectsUnparseableSize) {
	std::array<std::string, 5> args{"revenant-imagegen", "pattern", "out.img", "many", "lba"};
	EXPECT_FALSE(run(args));
}

TEST(ImagegenCli, RejectsUnknownPattern) {
	std::array<std::string, 5> args{"revenant-imagegen", "pattern", "out.img", "1024", "noise"};
	EXPECT_FALSE(run(args));
}

TEST(ImagegenCli, RejectsAnUnknownSubcommand) {
	std::array<std::string, 5> args{"revenant-imagegen", "fat32", "out.img", "1024", "lba"};
	EXPECT_FALSE(run(args));
}

TEST(ImagegenCli, GeneratesTheNtfsFixtureVolume) {
	const auto path = tempImage("revenant-cli-ntfs.img");
	std::array<std::string, 3> args{"revenant-imagegen", "ntfs", path};
	EXPECT_TRUE(runNamed(args));
	EXPECT_EQ(
		std::filesystem::file_size(path),
		revenant::imagegen::ntfs::makeLayout().totalBytes());
	std::filesystem::remove(path);
}

TEST(ImagegenCli, RejectsTheNtfsSubcommandWithoutAnOutputPath) {
	std::array<std::string, 5> args{"revenant-imagegen", "ntfs", "a", "b", "c"};
	EXPECT_FALSE(run(args));
}

TEST(ImagegenCli, GeneratesTheWholeDiskFixture) {
	const auto path = tempImage("revenant-cli-disk.img");
	std::array<std::string, 3> args{"revenant-imagegen", "disk", path};
	EXPECT_TRUE(runNamed(args));
	EXPECT_EQ(
		std::filesystem::file_size(path),
		revenant::imagegen::disk::buildMbrDiskImage().bytes.size());
	std::filesystem::remove(path);
}

TEST(ImagegenCli, GeneratesTheCarveCorpusAtTheRequestedSize) {
	const auto path = tempImage("revenant-cli-carve.img");
	std::array<std::string, 4> args{"revenant-imagegen", "carve", path, "16384"};
	EXPECT_TRUE(runSized(args));
	EXPECT_EQ(std::filesystem::file_size(path), 16384U);
	std::filesystem::remove(path);
}

TEST(ImagegenCli, RejectsACarveCorpusOfAnUnparseableSize) {
	std::array<std::string, 4> args{"revenant-imagegen", "carve", "out.img", "plenty"};
	EXPECT_FALSE(runSized(args));
}

TEST(ImagegenCli, GeneratesAnNtfsVolumeScaledToARecordCount) {
	const auto path = tempImage("revenant-cli-ntfs-big.img");
	std::array<std::string, 4> args{"revenant-imagegen", "ntfs", path, "256"};
	EXPECT_TRUE(runSized(args));
	EXPECT_EQ(
		std::filesystem::file_size(path),
		revenant::imagegen::ntfs::makeLayoutForRecords(256).totalBytes());
	std::filesystem::remove(path);
}

// Fewer records than the fixture itself holds would drop its own files.
TEST(ImagegenCli, RefusesAnNtfsVolumeSmallerThanTheFixture) {
	std::array<std::string, 4> args{"revenant-imagegen", "ntfs", "out.img", "4"};
	EXPECT_FALSE(runSized(args));
}

} // namespace
