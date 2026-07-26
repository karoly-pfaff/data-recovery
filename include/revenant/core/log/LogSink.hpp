// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "revenant/core/log/LogLevel.hpp"

namespace revenant {

// Destination seam for log records; injected into Logger (DIP — tests capture,
// tools write to stderr, future sinks write files).
class LogSink {
public:
	virtual ~LogSink() = default;
	LogSink() = default;
	LogSink(const LogSink&) = delete;
	LogSink& operator=(const LogSink&) = delete;
	LogSink(LogSink&&) = delete;
	LogSink& operator=(LogSink&&) = delete;

	virtual void write(LogLevel level, std::string_view message) = 0;
};

} // namespace revenant
