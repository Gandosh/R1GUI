// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the PNG codec of the toolkit: a stored-block encoder (8-bit RGBA) for screenshots and a
//   decoder (inflate, all five scanline filters, 8-bit grey / grey+alpha / RGB / palette / RGBA,
//   no interlacing) for reference images.
// Why: visual tests compare widget renders with the reference PNG crops under tests/reference and
//   write their own renders as PNG; neither needs a compression library, and the files read here
//   come from disk, so the decoder is a hostile-input boundary.
// Callers: image::compare / the visual harness, tests, examples/preview (panel screenshots).
// Failure behavior: decoding never throws and never over-allocates: it returns an error string for
//   anything malformed (bad signature, CRC, chunk order, size above kMaxPngSide, truncated or
//   oversized deflate stream, bad filter, trailing data in the pixel stream). The encoder throws
//   std::invalid_argument for an inconsistent size and std::runtime_error when a file cannot be written.
// Pixel format everywhere: RGBA, 8 bits, straight alpha, row 0 at the top, tightly packed.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace r1ui::widgets::image {

inline constexpr uint32_t kMaxPngSide = 16384;
inline constexpr size_t kMaxPngPixels = size_t{1} << 27;  // 128 Mpx: bounds decoder memory

struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;  // width * height * 4 bytes
};

struct DecodeResult {
  std::optional<Image> image;
  std::string error;  // empty on success
  bool ok() const { return image.has_value(); }
};

std::vector<uint8_t> encodePng(uint32_t width, uint32_t height, std::span<const uint8_t> rgba);
void writePng(const std::filesystem::path& path, uint32_t width, uint32_t height, std::span<const uint8_t> rgba);

DecodeResult decodePng(std::span<const uint8_t> bytes);
DecodeResult loadPng(const std::filesystem::path& path);

}  // namespace r1ui::widgets::image
