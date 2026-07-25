// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/log/Logger.hpp"

#include <string_view>

#include "revenant/core/log/LogLevel.hpp"
#include "revenant/core/log/LogSink.hpp"

namespace revenant {

Logger::Logger(LogSink& sink, LogLevel minLevel) noexcept : sink_(&sink), minLevel_(minLevel) {}

void Logger::log(LogLevel level, std::string_view message) {
    if (level < minLevel_) {
        return;
    }
    sink_->write(level, message);
}

} // namespace revenant
