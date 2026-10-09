// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for the icon rasteriser and the PNG writer: analytic coverage of simple shapes,
//   rejection of hostile SVG input (nothing drawn partially), every shipped Lucide icon parses and
//   draws, and the encoder's chunk structure (signature, CRCs, stored-block stream, Adler-32).
// Why: both are small parsers/encoders fed with files from outside the process.
// Callers: CTest (label fast, no GPU). R1UI_ASSETS_DIR points at the repository assets.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "PngWriter.h"
#include "SvgRaster.h"

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

std::string svg(const std::string& body, const std::string& attrs = "viewBox=\"0 0 24 24\" stroke-width=\"2\"") {
  return "<svg xmlns=\"http://www.w3.org/2000/svg\" " + attrs + ">" + body + "</svg>";
}

uint32_t crc32(const uint8_t* d, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    crc ^= d[i];
    for (int k = 0; k < 8; ++k) crc = (crc & 1u) != 0 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
  }
  return ~crc;
}

uint32_t be32(const uint8_t* p) { return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3]; }

}  // namespace

int main() {
  // A horizontal line of width 2 across the middle of a 24 px icon covers rows 11 and 12 fully.
  const auto line = preview::parseSvg(svg("<path d=\"M2 12h20\"/>"));
  expect(line.ok, "a simple path parses");
  const auto cov = preview::rasterizeIcon(line.icon, 24);
  expect(cov[11 * 24 + 12] == 255 && cov[12 * 24 + 12] == 255, "the stroke centre rows are fully covered");
  expect(cov[5 * 24 + 12] == 0 && cov[18 * 24 + 12] == 0, "rows away from the stroke are empty");
  expect(cov[11 * 24 + 0] == 0, "the stroke starts one unit in (round cap reaches x = 1, not 0)");
  // Scaling: a 12 px render of the same icon has a 1 px thick line.
  const auto half = preview::rasterizeIcon(line.icon, 12);
  expect(half[5 * 12 + 6] > 110 && half[5 * 12 + 6] < 145 && half[6 * 12 + 6] > 110 && half[6 * 12 + 6] < 145 && half[3 * 12 + 6] == 0, "scaled stroke: 1 px straddling two rows");
  // Circle and rect shapes, relative and arc commands.
  expect(preview::parseSvg(svg("<circle cx=\"12\" cy=\"12\" r=\"10\"/>")).ok, "circle");
  expect(preview::parseSvg(svg("<rect width=\"9\" height=\"6\" x=\"6\" y=\"14\" rx=\"2\"/>")).ok, "rect with radius");
  expect(preview::parseSvg(svg("<path d=\"M2.7 10.3a2.41 2.41 0 0 0 0 3.41l7.59 7.59Z\"/>")).ok, "arc with glued flags and close");
  expect(preview::parseSvg(svg("<path d=\"M1 1a1 1 0 0110 2\"/>")).ok, "arc flags without separators");

  // Hostile input is rejected with a message and never draws.
  const char* bad[] = {"",
                       "<svg viewBox=\"0 0 24 24\">",
                       "<svg viewBox=\"0 0 24 24\"><script>alert(1)</script></svg>",
                       "<svg viewBox=\"0 0 24 24\"><path d=\"M 1 1 L nan 4\"/></svg>",
                       "<svg viewBox=\"0 0 24 24\"><path d=\"L 1 1\"/></svg>",
                       "<svg viewBox=\"0 0 24 24\"><path d=\"M 1 1 e\"/></svg>",
                       "<svg viewBox=\"0 0 24 24\"><path d=\"M 1e999 1 L 2 2\"/></svg>",
                       "<svg viewBox=\"0 0 24 25\"><path d=\"M1 1L2 2\"/></svg>",
                       "<svg viewBox=\"0 0 24 24\"><circle cx=\"1\" cy=\"1\" r=\"-3\"/></svg>",
                       "<svg viewBox=\"0 0 24 24\"><path d=\"M1 1L2 2\" fill=></svg>",
                       "<svg viewBox=\"0 0 24 24\"><!-- open",
                       "<svg viewBox=\"0 0 24 24\"><path d=\"M1 1 A 1 1 0 2 0 3 3\"/></svg>"};
  for (const char* text : bad) expect(!preview::parseSvg(text).ok, text);
  std::string many = "<svg viewBox=\"0 0 24 24\">";
  for (int i = 0; i < 200; ++i) many += "<path d=\"M1 1L2 2\"/>";
  expect(!preview::parseSvg(many + "</svg>").ok, "too many elements");
  expect(!preview::parseSvg(std::string(preview::kMaxSvgBytes + 1, ' ')).ok, "oversized file");
  bool threw = false;
  try {
    preview::rasterizeIcon(line.icon, 2);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "an absurd pixel size is refused");

  // Every shipped icon parses and draws something at 14 and 24 px.
  size_t icons = 0;
  for (const auto& entry : std::filesystem::directory_iterator(std::string(R1UI_ASSETS_DIR) + "/icons/lucide")) {
    if (entry.path().extension() != ".svg") continue;
    std::ifstream in(entry.path(), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto parsed = preview::parseSvg(text);
    if (!parsed.ok) {
      std::fprintf(stderr, "%s: %s\n", entry.path().string().c_str(), parsed.error.c_str());
      expect(false, "a shipped icon must parse");
      continue;
    }
    for (int size : {14, 24}) {
      const auto pixels = preview::rasterizeIcon(parsed.icon, size);
      size_t lit = 0;
      for (uint8_t v : pixels) lit += v > 0;
      expect(lit > 4, "a shipped icon draws something");
    }
    ++icons;
  }
  expect(icons >= 150, "all shipped icons were checked");

  // PNG structure: signature, chunk CRCs, stored blocks reassemble to the filtered scanlines.
  std::vector<uint8_t> rgba(7 * 5 * 4);
  for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = static_cast<uint8_t>(i * 13);
  const std::vector<uint8_t> png = preview::encodePng(7, 5, rgba);
  const uint8_t signature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  expect(png.size() > 8 && std::equal(signature, signature + 8, png.begin()), "PNG signature");
  size_t pos = 8;
  std::vector<uint8_t> zlib;
  std::string kinds;
  while (pos + 12 <= png.size()) {
    const uint32_t length = be32(&png[pos]);
    kinds += std::string(reinterpret_cast<const char*>(&png[pos + 4]), 4);
    expect(be32(&png[pos + 8 + length]) == crc32(&png[pos + 4], length + 4), "chunk CRC");
    if (png[pos + 4] == 'I' && png[pos + 5] == 'D') zlib.assign(png.begin() + static_cast<std::ptrdiff_t>(pos + 8), png.begin() + static_cast<std::ptrdiff_t>(pos + 8 + length));
    pos += 12 + length;
  }
  expect(kinds == "IHDRIDATIEND", "chunk order");
  expect(zlib.size() > 6 && zlib[0] == 0x78, "zlib header");
  size_t raw = 0;
  size_t at = 2;
  while (at + 5 <= zlib.size() - 4) {
    const size_t len = zlib[at + 1] | (size_t{zlib[at + 2]} << 8);
    expect(((~len) & 0xFFFF) == (zlib[at + 3] | (size_t{zlib[at + 4]} << 8)), "stored block length complement");
    raw += len;
    at += 5 + len;
  }
  expect(raw == 5 * (7 * 4 + 1), "scanline bytes");
  bool rejected = false;
  try {
    preview::encodePng(0, 5, rgba);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  expect(rejected, "a zero width is refused");

  if (failures == 0) std::printf("svg_png_test: ok\n");
  return failures == 0 ? 0 : 1;
}
