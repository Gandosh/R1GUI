// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: loading of ".r1img" raw images (written by tools/spec/png_to_raw.py): the 8-byte magic
//   "R1IMG001", u32 little-endian width, u32 little-endian height, then width*height*4 bytes of
//   BGRA, 8 bits per channel.
// Why: the viewer shows reference screenshots without a PNG decoder in the toolkit.
// Callers: examples/preview/main.cpp. Failure behavior: returns an error string; never throws on
//   bad content; the whole file must match the header exactly (no short or trailing data).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct RawImage {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> bgra;  // width * height * 4 bytes
};

struct RawImageResult {
  std::optional<RawImage> image;
  std::string error;  // empty on success
};

RawImageResult loadRawImage(const std::filesystem::path& path);
