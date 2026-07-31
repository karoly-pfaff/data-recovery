// SPDX-License-Identifier: GPL-3.0-or-later
// Duplication-gate fixture: an on-disk layout written down as offsets. Its
// twin next door has the same shape and none of the same knowledge — the
// detector unifies identifiers *and* keywords and collapses literals, so every
// run of constants in the tree hashes like every other. There is no code here,
// and the gate is about duplicated code.

#include <cstdint>

namespace fixture::alpha {

inline constexpr std::uint64_t kSignatureOffset = 0x00;
inline constexpr std::uint64_t kVersionOffset = 0x04;
inline constexpr std::uint64_t kBlockSizeOffset = 0x08;
inline constexpr std::uint64_t kBlockCountOffset = 0x10;
inline constexpr std::uint64_t kRootNodeOffset = 0x18;
inline constexpr std::uint64_t kFreeListOffset = 0x20;
inline constexpr std::uint64_t kChecksumOffset = 0x28;
inline constexpr std::uint64_t kFlagsOffset = 0x2C;
inline constexpr std::uint64_t kGenerationOffset = 0x30;
inline constexpr std::uint64_t kLabelOffset = 0x38;
inline constexpr std::uint64_t kCreatedOffset = 0x48;
inline constexpr std::uint64_t kModifiedOffset = 0x50;
inline constexpr std::uint64_t kNodeSizeOffset = 0x58;
inline constexpr std::uint64_t kNodeCountOffset = 0x5C;

} // namespace fixture::alpha
