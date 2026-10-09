// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PngWriter.h: chunk framing, CRC-32, Adler-32 and stored deflate blocks.
// Invariants: every chunk length and the zlib stream fit 32-bit fields (checked); the stream is
//   exactly what a zlib decoder expects (header 0x78 0x01, blocks of at most 65535 bytes, Adler-32).
// Callers: PanelShot.cpp, tests/preview.
#include "PngWriter.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

#include "r1ui/core/CheckedCast.h"

namespace preview {

namespace {

constexpr size_t kMaxStoredBlock = 65535;

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0xFFFFFFFFu) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  return crc;
}

void putU32(std::vector<uint8_t>& out, uint32_t v) {
  for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>(v >> shift));
}

void putChunk(std::vector<uint8_t>& out, const char (&type)[5], const std::vector<uint8_t>& body) {
  putU32(out, r1ui::core::checkedCast<uint32_t>(body.size()));
  const size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), body.begin(), body.end());
  putU32(out, ~crc32(out.data() + start, out.size() - start));
}

uint32_t adler32(const std::vector<uint8_t>& data) {
  uint32_t a = 1;
  uint32_t b = 0;
  for (uint8_t byte : data) {
    a = (a + byte) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

}  // namespace

std::vector<uint8_t> encodePng(uint32_t width, uint32_t height, std::span<const uint8_t> rgba) {
  if (width == 0 || height == 0 || rgba.size() != size_t{width} * height * 4) throw std::invalid_argument("encodePng: bad size");
  // Scanlines: filter byte 0 followed by the row's RGBA bytes.
  std::vector<uint8_t> raw;
  raw.reserve(size_t{height} * (size_t{width} * 4 + 1));
  for (uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);
    const uint8_t* row = rgba.data() + size_t{y} * width * 4;
    raw.insert(raw.end(), row, row + size_t{width} * 4);
  }
  std::vector<uint8_t> zlib{0x78, 0x01};
  for (size_t offset = 0; offset < raw.size(); offset += kMaxStoredBlock) {
    const size_t length = std::min(kMaxStoredBlock, raw.size() - offset);
    const bool last = offset + length == raw.size();
    zlib.push_back(last ? 1 : 0);
    zlib.push_back(static_cast<uint8_t>(length & 0xFF));
    zlib.push_back(static_cast<uint8_t>(length >> 8));
    zlib.push_back(static_cast<uint8_t>(~length & 0xFF));
    zlib.push_back(static_cast<uint8_t>((~length >> 8) & 0xFF));
    zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
  }
  putU32(zlib, adler32(raw));

  std::vector<uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> header;
  putU32(header, width);
  putU32(header, height);
  header.insert(header.end(), {8, 6, 0, 0, 0});  // 8 bit, RGBA, deflate, adaptive filter, no interlace
  putChunk(png, "IHDR", header);
  putChunk(png, "IDAT", zlib);
  putChunk(png, "IEND", {});
  return png;
}

void writePng(const std::filesystem::path& path, uint32_t width, uint32_t height, std::span<const uint8_t> rgba) {
  const std::vector<uint8_t> bytes = encodePng(width, height, rgba);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), r1ui::core::checkedCast<std::streamsize>(bytes.size()));
  out.flush();
  if (!out) throw std::runtime_error("cannot write " + path.string());
}

}  // namespace preview
