// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>

#include "imagegen/CliMain.hpp"

namespace {

bool run(std::array<std::string, 4>& args) {
	// .at() (not operator[]) so bounds are checked even though every index
	// here is a compile-time-fixed, in-range constant.
	std::array<char*, 4> argv{
		args.at(0).data(),
		args.at(1).data(),
		args.at(2).data(),
		args.at(3).data()};
	return revenant::imagegen::runCli(argv);
}

TEST(ImagegenCli, GeneratesImageFromValidArguments) {
	const auto path = (std::filesystem::temp_directory_path() / "revenant-cli-test.img").string();
	std::array<std::string, 4> args{"revenant-imagegen", path, "1024", "lba"};
	EXPECT_TRUE(run(args));
	EXPECT_EQ(std::filesystem::file_size(path), 1024U);
	std::filesystem::remove(path);
}

TEST(ImagegenCli, RejectsUnparseableSize) {
	std::array<std::string, 4> args{"revenant-imagegen", "out.img", "many", "lba"};
	EXPECT_FALSE(run(args));
}

TEST(ImagegenCli, RejectsUnknownPattern) {
	std::array<std::string, 4> args{"revenant-imagegen", "out.img", "1024", "noise"};
	EXPECT_FALSE(run(args));
}

} // namespace
