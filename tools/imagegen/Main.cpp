// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <span>

#include "imagegen/CliMain.hpp"

int main(int argc, char** argv) {
    const std::span<char* const> args{argv, static_cast<std::size_t>(argc)};
    return revenant::imagegen::runCli(args) ? 0 : 1;
}
