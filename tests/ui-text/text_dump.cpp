// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ui-text-dump, a headless helper that runs the whole text pipeline (shape, rasterize,
//   atlas, quads) on one string and prints the result as ASCII art, optionally also writing the
//   composed canvas as a raw 8-bit coverage file (width*height bytes) for external viewers.
// Why: lets a human eyeball glyph shapes, spacing and synthetic bold without a window or GPU.
// Usage: ui-text-dump [--size N] [--weight W] [--bold none|synthetic|real] [--x PEN] [--raw FILE]
//                     [--atlas FILE] TEXT
//   Defaults: size 12, weight 400. synthetic bold uses the Regular face plus outline embolden for
//   weights >= 500; real uses the matching Inter face. --atlas writes the whole atlas image.
// Not a test: it is not registered with CTest. Exit code 0 on success, 2 on usage/load errors.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "r1ui/text/Font.h"
#include "r1ui/text/GlyphAtlas.h"
#include "r1ui/text/GlyphQuads.h"
#include "r1ui/text/Shaper.h"

using namespace r1ui::text;

namespace {

struct FaceFile {
  const char* file;
  int weight;
};
constexpr FaceFile kFaces[] = {{"Inter-Regular.ttf", 400}, {"Inter-Medium.ttf", 500}, {"Inter-SemiBold.ttf", 600},
                               {"Inter-Bold.ttf", 700}, {"Inter-ExtraBold.ttf", 800}};

int usage() {
  std::fprintf(stderr,
               "usage: ui-text-dump [--size N] [--weight W] [--bold none|synthetic|real] [--x PEN] "
               "[--raw FILE] [--atlas FILE] TEXT\n");
  return 2;
}

void writeRaw(const std::string& path, const std::uint8_t* data, std::size_t size) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
}

}  // namespace

int main(int argc, char** argv) {
  float size = 12.0f;
  int weight = 400;
  float penX = 2.0f;
  std::string boldMode = "synthetic";
  std::string rawPath;
  std::string atlasPath;
  std::string text;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    if (a == "--size" || a == "--weight" || a == "--x" || a == "--bold" || a == "--raw" || a == "--atlas") {
      const char* v = value();
      if (v == nullptr) return usage();
      if (a == "--size") size = static_cast<float>(std::atof(v));
      if (a == "--weight") weight = std::atoi(v);
      if (a == "--x") penX = static_cast<float>(std::atof(v));
      if (a == "--bold") boldMode = v;
      if (a == "--raw") rawPath = v;
      if (a == "--atlas") atlasPath = v;
    } else {
      text = a;
    }
  }
  if (text.empty()) return usage();

  FontLibrary lib;
  FontFamily family;
  for (const FaceFile& f : kFaces) {
    auto r = lib.loadFromFile(std::string(R1UI_ASSETS_DIR) + "/fonts/" + f.file, f.weight);
    if (!r.ok() || !family.addFace(r.value()).ok()) {
      std::fprintf(stderr, "cannot load %s: %s\n", f.file, r.ok() ? "family" : r.error().message.c_str());
      return 2;
    }
  }
  const BoldMode mode = boldMode == "real" ? BoldMode::RealFaces : BoldMode::Synthetic;
  const ResolvedFace face = family.resolve(boldMode == "none" ? 400 : weight, size, mode);
  if (!face.font) {
    std::fprintf(stderr, "invalid size\n");
    return 2;
  }

  auto run = shapeText(*face.font, size, text);
  if (!run.ok()) {
    std::fprintf(stderr, "shaping failed: %s\n", run.error().message.c_str());
    return 2;
  }
  auto atlasResult = GlyphAtlas::create();
  GlyphAtlas atlas = std::move(atlasResult.value());
  atlas.beginFrame();
  const FontMetrics metrics = face.font->metricsAt(size);
  const int baseline = static_cast<int>(metrics.ascent) + 2;
  const int canvasH = baseline + static_cast<int>(metrics.descent) + 3;
  const int canvasW = static_cast<int>(run.value().width + penX) + 4;
  QuadParams qp;
  qp.pixelSize = size;
  qp.emboldenPx = face.emboldenPx;
  std::vector<GlyphQuad> quads;
  auto stats = buildGlyphQuads(atlas, *face.font, run.value(), qp, penX, static_cast<float>(baseline), quads);
  if (!stats.ok()) {
    std::fprintf(stderr, "quad building failed: %s\n", stats.error().message.c_str());
    return 2;
  }

  // Compose the quads like a renderer would: nearest sampling of the atlas, additive coverage.
  std::vector<std::uint8_t> canvas(static_cast<std::size_t>(canvasW) * static_cast<std::size_t>(canvasH), 0);
  for (const GlyphQuad& q : quads) {
    const int sx = static_cast<int>(q.u0 * static_cast<float>(atlas.width()) + 0.5f);
    const int sy = static_cast<int>(q.v0 * static_cast<float>(atlas.height()) + 0.5f);
    for (int y = 0; y < static_cast<int>(q.h); ++y) {
      for (int x = 0; x < static_cast<int>(q.w); ++x) {
        const int dx = static_cast<int>(q.x) + x;
        const int dy = static_cast<int>(q.y) + y;
        if (dx < 0 || dy < 0 || dx >= canvasW || dy >= canvasH) continue;
        const int v = atlas.pixels()[static_cast<std::size_t>(sy + y) * static_cast<std::size_t>(atlas.width()) +
                                     static_cast<std::size_t>(sx + x)];
        std::uint8_t& dst = canvas[static_cast<std::size_t>(dy) * static_cast<std::size_t>(canvasW) + static_cast<std::size_t>(dx)];
        dst = static_cast<std::uint8_t>(std::min(255, dst + v));
      }
    }
  }

  std::printf("text \"%s\" size %.2f weight %d mode %s embolden %.3f px, width %.3f px, %zu glyphs, %zu quads\n",
              text.c_str(), static_cast<double>(size), weight, boldMode.c_str(), static_cast<double>(face.emboldenPx),
              static_cast<double>(run.value().width), run.value().glyphs.size(), quads.size());
  std::printf("ascent %.2f descent %.2f xHeight %.2f capHeight %.2f, baseline row %d\n",
              static_cast<double>(metrics.ascent), static_cast<double>(metrics.descent),
              static_cast<double>(metrics.xHeight), static_cast<double>(metrics.capHeight), baseline);
  static const char ramp[] = " .:-=+*#%@";
  for (int y = 0; y < canvasH; ++y) {
    std::string line;
    for (int x = 0; x < canvasW; ++x) {
      const int v = canvas[static_cast<std::size_t>(y) * static_cast<std::size_t>(canvasW) + static_cast<std::size_t>(x)];
      line.push_back(ramp[v * 9 / 255]);
      line.push_back(ramp[v * 9 / 255]);
    }
    std::printf("%s%s\n", line.c_str(), y == baseline - 1 ? "   <- baseline above this row" : "");
  }
  if (!rawPath.empty()) {
    writeRaw(rawPath, canvas.data(), canvas.size());
    std::printf("wrote %dx%d R8 canvas to %s\n", canvasW, canvasH, rawPath.c_str());
  }
  if (!atlasPath.empty()) {
    writeRaw(atlasPath, atlas.pixels(), static_cast<std::size_t>(atlas.width()) * static_cast<std::size_t>(atlas.height()));
    std::printf("wrote %dx%d R8 atlas to %s\n", atlas.width(), atlas.height(), atlasPath.c_str());
  }
  return 0;
}
