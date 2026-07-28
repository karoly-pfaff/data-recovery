// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <span>

#include "cli/UndeleteCli.hpp"

int main(int argc, char** argv) {
	const std::span<char* const> args{argv, static_cast<std::size_t>(argc)};
	return revenant::cli::runUndeleteCli(args) ? 0 : 1;
}
