// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <streambuf>

namespace revenant::testing {

// A stream buffer that takes `limit` bytes and then refuses every one after it.
//
// The imagegen writers' failure paths are otherwise unreachable from a unit
// test — a full disk is not something a test can arrange — and an offset
// reported from a failed write is exactly the kind of number nobody checks
// until it is wrong.
//
// It models a failing *stream*, which is the narrower half of a full disk: with
// no put area, `xsputn` drives `overflow` a byte at a time, so the refusal is
// exact and immediate. It cannot model a failure that only surfaces at flush;
// that half is reached through a path that cannot be opened at all.
class FailingBuf final : public std::streambuf {
public:
	explicit FailingBuf(std::size_t limit) noexcept : left_(limit) {}

protected:
	int_type overflow(int_type value) override {
		if (left_ == 0) {
			return traits_type::eof();
		}
		--left_;
		return value;
	}

private:
	std::size_t left_;
};

} // namespace revenant::testing
