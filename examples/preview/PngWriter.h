// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: a minimal PNG encoder (8-bit RGBA, no filtering, stored deflate blocks) for the preview's
//   offscreen screenshots.
// Why: the visual comparison tool tools/spec/imgdiff.py reads PNGs; a stored-block encoder is a few
//   dozen lines and needs no compression library. Files are large (about the raw size) but only
//   written on request.
// Callers: PanelShot.cpp, tests/preview. Failure behavior: throws std::runtime_error when the file
//   cannot be written and std::invalid_argument for an inconsistent size.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace preview {

// RGBA pixels, row 0 at the top, tightly packed (width * height * 4 bytes).
std::vector<uint8_t> encodePng(uint32_t width, uint32_t height, std::span<const uint8_t> rgba);
void writePng(const std::filesystem::path& path, uint32_t width, uint32_t height, std::span<const uint8_t> rgba);

}  // namespace preview
