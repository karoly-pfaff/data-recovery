// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/carve/BuiltinCarvers.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <string_view>

#include "formats/JpegCarver.hpp"
#include "formats/Mp4Carver.hpp"
#include "formats/PdfCarver.hpp"
#include "formats/PngCarver.hpp"
#include "formats/RawCarver.hpp"
#include "formats/ZipCarver.hpp"
#include "revenant/carve/CarverRegistry.hpp"

namespace revenant::carve {

namespace {

// Every extension a carver can report, so an allowlist naming any one of them
// keeps that carver.
constexpr std::array<std::string_view, 1> kJpegNames{"jpg"};
constexpr std::array<std::string_view, 1> kPngNames{"png"};
constexpr std::array<std::string_view, 2> kMp4Names{"mp4", "mov"};
constexpr std::array<std::string_view, 4> kRawNames{"cr2", "nef", "arw", "tif"};
constexpr std::array<std::string_view, 4> kZipNames{"zip", "docx", "xlsx", "pptx"};
constexpr std::array<std::string_view, 1> kPdfNames{"pdf"};

[[nodiscard]] bool
allowed(std::span<const std::string_view> names, std::span<const std::string_view> allowlist) {
	return allowlist.empty() || std::ranges::any_of(names, [allowlist](std::string_view name) {
			   return std::ranges::find(allowlist, name) != allowlist.end();
		   });
}

// The registry sits between the two spans on purpose: two adjacent parameters
// of the same type would be a swap waiting to happen.
template <typename Carver>
void addIfAllowed(
	std::span<const std::string_view> names,
	CarverRegistry& registry,
	std::span<const std::string_view> allowlist) {
	if (allowed(names, allowlist)) {
		registry.registerCarver(std::make_unique<Carver>());
	}
}

} // namespace

void registerBuiltinCarvers(CarverRegistry& registry) {
	registerBuiltinCarvers(registry, {});
}

void registerBuiltinCarvers(CarverRegistry& registry, std::span<const std::string_view> allowlist) {
	addIfAllowed<JpegCarver>(kJpegNames, registry, allowlist);
	addIfAllowed<PngCarver>(kPngNames, registry, allowlist);
	addIfAllowed<Mp4Carver>(kMp4Names, registry, allowlist);
	addIfAllowed<RawCarver>(kRawNames, registry, allowlist);
	addIfAllowed<ZipCarver>(kZipNames, registry, allowlist);
	addIfAllowed<PdfCarver>(kPdfNames, registry, allowlist);
}

} // namespace revenant::carve
