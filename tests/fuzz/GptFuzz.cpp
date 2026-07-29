// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any bytes offered as a GPT-partitioned disk enumerate to a list of
// byte ranges or to a typed error — never a crash and never an allocation the
// input chose. Where the entry array is, how many entries it holds and how large
// one is are all read from the same bytes the array is then checksummed against,
// so an unbounded count or an unchecked LBA-to-byte product would show up here.
// The backup path is on the same input: a disk whose primary copy fails sends
// the read to its last sector, which is another attacker-chosen header.
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "revenant/volume/GptPartitions.hpp"
#include "support/FuzzInput.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

constexpr std::uint32_t kSectorSize = 512;

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	auto bytes = revenant::testing::toByteVector(std::span<const std::uint8_t>{data, size});
	revenant::testing::InMemoryDevice device{std::move(bytes), kSectorSize};
	static_cast<void>(revenant::volume::readGptPartitions(device));
	return 0;
}
