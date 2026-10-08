// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the .r1img parser declared in RawImage.h. Validates magic, dimension bounds and the exact
//   file size before allocating, so a damaged file cannot cause a huge allocation.
// Callers: examples/preview/main.cpp.
#include "RawImage.h"

#include <cstring>
#include <fstream>
#include <utility>

namespace {

constexpr char kMagic[8] = {'R', '1', 'I', 'M', 'G', '0', '0', '1'};
constexpr size_t kHeaderBytes = 16;
constexpr uint32_t kMaxDimension = 16384;

uint32_t readU32(const uint8_t* p) {
  return uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
}

RawImageResult failure(const std::filesystem::path& path, const char* why) {
  RawImageResult result;
  result.error = path.string() + ": " + why;
  return result;
}

}  // namespace

RawImageResult loadRawImage(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return failure(path, "cannot open file");
  uint8_t header[kHeaderBytes];
  file.read(reinterpret_cast<char*>(header), kHeaderBytes);
  if (file.gcount() != static_cast<std::streamsize>(kHeaderBytes)) return failure(path, "file too short");
  if (std::memcmp(header, kMagic, sizeof(kMagic)) != 0) return failure(path, "bad magic");

  const uint32_t width = readU32(header + 8);
  const uint32_t height = readU32(header + 12);
  if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension) {
    return failure(path, "image dimensions out of range");
  }
  const uint64_t pixelBytes = uint64_t{width} * height * 4;  // < 2^30 given the bounds above

  file.seekg(0, std::ios::end);
  const std::streamoff total = file.tellg();
  if (total < 0 || static_cast<uint64_t>(total) != kHeaderBytes + pixelBytes) {
    return failure(path, "file size does not match its header");
  }
  file.seekg(static_cast<std::streamoff>(kHeaderBytes), std::ios::beg);

  RawImage image;
  image.width = width;
  image.height = height;
  image.bgra.resize(static_cast<size_t>(pixelBytes));
  file.read(reinterpret_cast<char*>(image.bgra.data()), static_cast<std::streamsize>(pixelBytes));
  if (file.gcount() != static_cast<std::streamsize>(pixelBytes)) return failure(path, "short read");

  RawImageResult result;
  result.image = std::move(image);
  return result;
}
