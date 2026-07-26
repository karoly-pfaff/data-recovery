// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "revenant/core/log/LogLevel.hpp"
#include "revenant/core/log/LogSink.hpp"

namespace revenant {

// Writes "[level] message" lines to stderr; the sink CLI tools use.
class StderrSink final : public LogSink {
public:
	void write(LogLevel level, std::string_view message) override;
};

} // namespace revenant
