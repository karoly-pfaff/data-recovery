// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "revenant/core/log/LogLevel.hpp"
#include "revenant/core/log/LogSink.hpp"

namespace revenant::testing {

// Test double: records every write so tests can assert on log output.
class CapturingLogSink final : public LogSink {
public:
    struct Record {
        LogLevel level{};
        std::string message;
    };

    void write(LogLevel level, std::string_view message) override {
        records_.push_back(Record{.level = level, .message = std::string{message}});
    }

    [[nodiscard]] const std::vector<Record>& records() const noexcept {
        return records_;
    }

private:
    std::vector<Record> records_;
};

} // namespace revenant::testing
