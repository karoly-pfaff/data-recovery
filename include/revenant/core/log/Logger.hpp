// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "revenant/core/log/LogLevel.hpp"
#include "revenant/core/log/LogSink.hpp"

namespace revenant {

// Leveled front door over an injected sink. Formatting is the caller's
// concern: pass a finished message.
class Logger {
public:
	Logger(LogSink& sink, LogLevel minLevel) noexcept;

	void log(LogLevel level, std::string_view message);

	[[nodiscard]] LogLevel minLevel() const noexcept {
		return minLevel_;
	}

private:
	LogSink* sink_; // never null; pointer (not reference) keeps Logger copyable
	LogLevel minLevel_;
};

} // namespace revenant
