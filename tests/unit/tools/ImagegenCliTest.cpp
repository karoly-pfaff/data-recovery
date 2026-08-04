// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "imagegen/CliMain.hpp"
#include "imagegen/SoakImage.hpp"
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

[[nodiscard]] std::vector<std::string> usageLines() {
	std::istringstream text{revenant::imagegen::usageText()};
	std::vector<std::string> lines;
	std::string line;
	while (std::getline(text, line)) {
		lines.push_back(line);
	}
	return lines;
}

// "Documents itself in the usage line" is a criterion, so it gets an assertion
// rather than a structural argument. The text is folded out of the verb table,
// so a verb cannot be dispatchable and undocumented — but it can be registered
// with an empty operand string, and then nothing would notice.
TEST(ImagegenCli, EveryDispatchableVerbHasAUsageLineWithOperands) {
	const auto lines = usageLines();
	ASSERT_EQ(lines.size(), 7U); // "usage:" and one line per dispatchable verb
	const auto documented = std::ranges::count_if(lines, [](const std::string& line) {
		return line.find('<') != std::string::npos;
	});
	EXPECT_EQ(documented, 6);
}

TEST(ImagegenCli, TheUsageTextNamesTheSoakVerb) {
	EXPECT_NE(revenant::imagegen::usageText().find("soak <output>"), std::string::npos);
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

TEST(ImagegenCli, GeneratesTheSoakFixtureAtTheRequestedSize) {
	const auto path = tempImage("revenant-cli-soak.img");
	std::array<std::string, 5> args{"revenant-imagegen", "soak", path, "1048576", "4"};
	EXPECT_TRUE(run(args));
	EXPECT_EQ(std::filesystem::file_size(path), 1048576U);
	std::filesystem::remove(path);
	std::filesystem::remove(revenant::imagegen::soakPlanPath(path));
}

// The size and the plant count are one request together: four plants do not fit
// in a kibibyte, and quietly planting fewer would make the plan file a lie.
TEST(ImagegenCli, RefusesASoakFixtureWithNoRoomForItsPlants) {
	std::array<std::string, 5> args{"revenant-imagegen", "soak", "out.img", "1024", "4"};
	EXPECT_FALSE(run(args));
}

// Fewer records than the fixture itself holds would drop its own files.
TEST(ImagegenCli, RefusesAnNtfsVolumeSmallerThanTheFixture) {
	std::array<std::string, 4> args{"revenant-imagegen", "ntfs", "out.img", "4"};
	EXPECT_FALSE(runSized(args));
}

} // namespace
