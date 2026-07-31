// SPDX-License-Identifier: GPL-3.0-or-later
// Duplication-gate fixture: see LayoutAlpha.cc. A different format's offsets,
// which is a different fact stated in the only shape C++ has for stating it.

#include <cstdint>

namespace fixture::beta {

inline constexpr std::uint64_t kMagicOffset = 0x00;
inline constexpr std::uint64_t kSectorBytesOffset = 0x0B;
inline constexpr std::uint64_t kClusterSectorsOffset = 0x0D;
inline constexpr std::uint64_t kReservedOffset = 0x0E;
inline constexpr std::uint64_t kTableCountOffset = 0x10;
inline constexpr std::uint64_t kMediaByteOffset = 0x15;
inline constexpr std::uint64_t kTotalSectorsOffset = 0x20;
inline constexpr std::uint64_t kTableLengthOffset = 0x24;
inline constexpr std::uint64_t kRootClusterOffset = 0x2C;
inline constexpr std::uint64_t kInfoSectorOffset = 0x30;
inline constexpr std::uint64_t kBackupSectorOffset = 0x32;
inline constexpr std::uint64_t kDriveNumberOffset = 0x40;
inline constexpr std::uint64_t kVolumeIdOffset = 0x43;
inline constexpr std::uint64_t kVolumeLabelOffset = 0x47;

} // namespace fixture::beta
