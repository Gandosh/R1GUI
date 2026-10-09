// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for the PNG codec and the image comparison: encode/decode round trip, decoding of the
//   real reference PNGs (dynamic Huffman, all filters), hostile files (bad signature, CRC, sizes,
//   decompression bombs, truncation, random corruption never crash or over-allocate) and the
//   imgdiff port (identical, perturbed block, tolerance, size mismatch, profile loading).
// Why: the decoder reads files from disk and the comparison decides every visual test; both must be
//   right before a widget is judged against a reference.
// Callers: CTest (label fast, no GPU). R1UI_REFERENCE_DIR points at tests/reference.
#include <cstring>
#include <random>

#include "TestSupport.h"
#include "r1ui/widgets/image/ImageDiff.h"
#include "r1ui/widgets/image/Png.h"

namespace {

using namespace r1ui::widgets::image;

uint32_t crc32(const uint8_t* d, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    crc ^= d[i];
    for (int k = 0; k < 8; ++k) crc = (crc & 1u) != 0 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
  }
  return ~crc;
}

void putBe32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

// Rewrites the IHDR size fields of an encoded PNG and fixes the chunk CRC (IHDR starts at byte 8).
std::vector<uint8_t> withHeaderSize(std::vector<uint8_t> png, uint32_t w, uint32_t h) {
  putBe32(&png[16], w);
  putBe32(&png[20], h);
  putBe32(&png[8 + 8 + 13], ~crc32(&png[12], 17));
  return png;
}

Image gradient(uint32_t w, uint32_t h) {
  Image img;
  img.width = w;
  img.height = h;
  img.rgba.resize(size_t{w} * h * 4);
  for (size_t i = 0; i < img.rgba.size(); ++i) img.rgba[i] = static_cast<uint8_t>(i * 7 + i / 13);
  return img;
}

void testRoundTripAndHostile() {
  const Image src = gradient(37, 19);
  const std::vector<uint8_t> png = encodePng(src.width, src.height, src.rgba);
  const DecodeResult back = decodePng(png);
  R1_EXPECT(back.ok() && back.image->width == 37 && back.image->height == 19 && back.image->rgba == src.rgba);

  R1_EXPECT(!decodePng({}).ok());
  std::vector<uint8_t> badSig = png;
  badSig[1] = 'X';
  R1_EXPECT(!decodePng(badSig).ok());
  std::vector<uint8_t> badCrc = png;
  badCrc[20] ^= 1;  // inside IHDR
  R1_EXPECT(!decodePng(badCrc).ok());
  R1_EXPECT(!decodePng(std::span<const uint8_t>(png).first(png.size() / 2)).ok());
  R1_EXPECT(!decodePng(withHeaderSize(png, 0, 19)).ok());
  R1_EXPECT(!decodePng(withHeaderSize(png, 0xFFFFFFFFu, 0xFFFFFFFFu)).ok());
  R1_EXPECT(!decodePng(withHeaderSize(png, 16384, 16384)).ok());  // claims 1 GiB: must be refused before inflating
  R1_EXPECT(!decodePng(withHeaderSize(png, 37, 5)).ok());         // stream holds more rows than the header allows
  R1_EXPECT(!decodePng(withHeaderSize(png, 37, 40)).ok());        // stream shorter than the header requires

  // Random corruption of the body never crashes (every result is either ok or an error string).
  std::mt19937 rng(12345);
  int rejected = 0;
  for (int i = 0; i < 300; ++i) {
    std::vector<uint8_t> mutated = png;
    for (int k = 0; k < 3; ++k) mutated[rng() % mutated.size()] = static_cast<uint8_t>(rng());
    const DecodeResult r = decodePng(mutated);
    if (!r.ok()) {
      ++rejected;
      R1_EXPECT(!r.error.empty());
    }
  }
  R1_EXPECT(rejected > 200);  // CRCs catch almost all of it

  bool threw = false;
  try {
    (void)encodePng(0, 5, std::vector<uint8_t>{});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  R1_EXPECT(threw);
}

void testReferenceFiles() {
  // Chrome-made files use dynamic Huffman blocks and adaptive filters.
  size_t count = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(r1test::referenceDir() / "openpencil" / "dark")) {
    if (entry.path().extension() != ".png" || entry.path().filename().string().rfind("widget-", 0) != 0) continue;
    const DecodeResult r = loadPng(entry.path());
    if (!r.ok()) std::fprintf(stderr, "%s\n", r.error.c_str());
    R1_EXPECT(r.ok() && r.image->width > 8 && r.image->height > 8);
    ++count;
  }
  R1_EXPECT(count >= 20);
  const DecodeResult icon = loadPng(r1test::referenceDir() / "icons" / "16" / "diamond.png");
  R1_EXPECT(icon.ok() && icon.image->width == 16 && icon.image->height == 16);
  R1_EXPECT(!loadPng(r1test::referenceDir() / "nope.png").ok());
}

void testDiff() {
  Image a = gradient(20, 10);
  for (size_t i = 0; i < a.rgba.size(); i += 4) a.rgba[i] = 50;  // uniform red channel: the +100 below cannot wrap
  Tolerance strict{2, 0.0};
  R1_EXPECT(compare(a, a, strict).pass);
  Image b = a;
  for (int y = 2; y < 5; ++y) {
    for (int x = 3; x < 8; ++x) b.rgba[(static_cast<size_t>(y) * 20 + static_cast<size_t>(x)) * 4] = 150;
  }
  const DiffMetrics m = compare(a, b, Tolerance{8, 0.01});
  R1_EXPECT(!m.pass && m.failingPixels == 15 && m.maxChannelDiff == 100 && m.boundsMinX == 3 && m.boundsMaxX == 7 && m.boundsMinY == 2 && m.boundsMaxY == 4);
  R1_EXPECT_NEAR(m.meanDiff, 1500.0 / 200.0, 1e-9);
  R1_EXPECT(compare(a, b, Tolerance{8, 0.1}).pass);        // 7.5% failing is within a 10% budget
  R1_EXPECT(compare(a, b, Tolerance{100, 0.0}).pass);      // a difference equal to the tolerance passes
  R1_EXPECT(!compare(a, gradient(21, 10), strict).error.empty());
  const Image diff = makeDiffImage(b, m);
  R1_EXPECT(diff.rgba[(2 * 20 + 3) * 4] == 255 && diff.rgba[(2 * 20 + 3) * 4 + 1] == 0);

  std::string error;
  const auto text = loadTolerance(r1test::referenceDir() / "tolerance.json", "text", error);
  R1_EXPECT(text && text->channelTolerance == 48 && text->maxFailingFraction == 0.03);
  const auto def = loadTolerance(r1test::referenceDir() / "tolerance.json", "default", error);
  R1_EXPECT(def && def->channelTolerance == 8);
  R1_EXPECT(!loadTolerance(r1test::referenceDir() / "tolerance.json", "nope", error) && !error.empty());
  R1_EXPECT(!loadTolerance(r1test::referenceDir() / "missing.json", "default", error));
}

}  // namespace

int main() {
  testRoundTripAndHostile();
  testReferenceFiles();
  testDiff();
  return r1test::finish();
}
