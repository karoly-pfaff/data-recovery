// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any bytes offered as a partitioned disk enumerate to a list of byte
// ranges or to a typed error — never a crash, and never a walk that does not
// end. The EBR chain is a linked list whose pointers come out of the same
// attacker-chosen bytes it is bounded by, so a missing revisit test or an
// unchecked length would show up here as a hang, and an unchecked LBA-to-byte
// product as undefined behaviour under the sanitizers.
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "revenant/volume/MbrPartitions.hpp"
#include "support/FuzzInput.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

constexpr std::uint32_t kSectorSize = 512;

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	auto bytes = revenant::testing::toByteVector(std::span<const std::uint8_t>{data, size});
	revenant::testing::InMemoryDevice device{std::move(bytes), kSectorSize};
	static_cast<void>(revenant::volume::readMbrPartitions(device));
	return 0;
}
