// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/log/StderrSink.hpp"

#include <iostream>
#include <string_view>

#include "revenant/core/log/LogLevel.hpp"

namespace revenant {

void StderrSink::write(LogLevel level, std::string_view message) {
    std::cerr << '[' << toString(level) << "] " << message << '\n';
}

} // namespace revenant
