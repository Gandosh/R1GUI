// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: golden-image test of the thumbnail grid (no OpenPencil reference exists for an asset browser):
//   the grid is rendered offscreen in both themes with generated items and a provider that makes a
//   vertical red-to-blue picture, then checked structurally: the selected tile fill (muted when the
//   grid is not focused), the thumbnail square, the type strip colour, the modified marker, the real
//   picture drawn on the GPU through GpuThumbnailTextures (red above, blue below), ink in the name
//   labels, the scrollbar thumb, and the plain background between tiles. The renders are written as
//   PNG into the artifact folder for inspection.
// Callers: CTest (thumbnailgrid gpu: renders offscreen on a Vulkan device, no window).
// Note: the picture needs a texture on the device that draws it, which the shared harness rig does
//   not expose, so this test owns a small rig of its own (device, textures, services).
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "TestSupport.h"
#include "r1ui/render/OffscreenTarget.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/widgets/gpu/GpuTextures.h"
#include "r1ui/widgets/image/Png.h"
#include "r1ui/widgets/thumbnailgrid/GpuThumbnailTextures.h"
#include "r1ui/widgets/thumbnailgrid/ThumbnailGrid.h"

namespace {

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;

constexpr int kPad = 6;
constexpr int kW = 700;
constexpr int kH = 420;
constexpr uint64_t kPictureKey = 105;  // the fifth item (index 4)

struct Rig {
  Rig() : factory(device), services(r1test::loadTokens(), factory, r1test::assetPaths()) {}
  r1ui::render::RenderDevice device;
  GpuTextureFactory factory;
  Services services;
};

// A 64 x 64 picture: red at the top fading to blue at the bottom.
class GradientProvider final : public thumbs::ThumbnailProvider {
 public:
  thumbs::ThumbnailStatus produce(uint64_t key, uint32_t, thumbs::ThumbnailImage& out) override {
    if (key != kPictureKey) return thumbs::ThumbnailStatus::Failed;
    out.width = out.height = 64;
    out.rgba.resize(64 * 64 * 4);
    for (uint32_t y = 0; y < 64; ++y) {
      for (uint32_t x = 0; x < 64; ++x) {
        uint8_t* p = &out.rgba[(static_cast<size_t>(y) * 64 + x) * 4];
        p[0] = static_cast<uint8_t>(255 - y * 4);
        p[1] = 40;
        p[2] = static_cast<uint8_t>(y * 4);
        p[3] = 255;
      }
    }
    return thumbs::ThumbnailStatus::Ready;
  }
};

class SampleModel final : public AssetModel {
 public:
  size_t count() const override { return 14; }
  void item(size_t i, GridItem& out) const override {
    out = GridItem{};
    out.key = 101 + i;
    out.name = i == 1 ? "A rather long asset name that must wrap" : "Asset " + std::to_string(i + 1);
    out.typeLabel = "Texture";
    out.icon = i == 0 ? "folder" : "image";
    out.folder = i == 0;
    out.modified = i == 3;
    out.typeColour = {0.20, 0.60, 0.30};
  }
};

struct Probe {
  const r1ui::widgets::image::Image& img;
  const uint8_t* at(int x, int y) const { return &img.rgba[(static_cast<size_t>(y) * img.width + static_cast<size_t>(x)) * 4]; }
  bool near(int x, int y, const r1ui::theme::Color& c, int tol) const {
    const uint8_t* p = at(x, y);
    return std::abs(p[0] - c.r) <= tol && std::abs(p[1] - c.g) <= tol && std::abs(p[2] - c.b) <= tol;
  }
};

void checkTheme(r1ui::theme::ThemeId theme, const char* name) {
  Rig rig;
  rig.services.theme().set(theme);
  SampleModel model;
  GradientProvider provider;
  GpuThumbnailTextures sink(rig.device);
  UiContext ui(rig.services);  // after the sink: the grid's pictures are released before the sink goes
  ui.setAnimationsEnabled(false);
  ui.setViewport(kW + 2 * kPad, kH + 2 * kPad, 1.0f);
  ui.setTime(1000);
  layout::Style& root = ui.rootStyle();
  root.direction = layout::FlexDirection::Column;
  root.alignItems = layout::Align::Start;
  for (double& p : root.padding) p = kPad;
  ThumbnailGrid& grid = ui.create<ThumbnailGrid>(ui.root());
  grid.style().width = layout::Length::px(kW);
  grid.style().height = layout::Length::px(kH);
  grid.style().flexGrow = 0.0;
  grid.setModel(&model);
  grid.setProvider(&provider);
  grid.setTextureSink(&sink);
  grid.setSelection({103, 107});
  ui.frame();

  r1ui::render::OffscreenTarget target(rig.device, kW + 2 * kPad, kH + 2 * kPad);
  const r1ui::render::Color clear = rig.services.color("panel");
  for (int i = 0; i < 8; ++i) {
    if (!renderFrame(ui, target, clear)) throw std::runtime_error("the offscreen frame was skipped");
    if (grid.hasThumbnail(kPictureKey)) break;
  }
  R1_EXPECT(grid.hasThumbnail(kPictureKey));
  if (!renderFrame(ui, target, clear)) throw std::runtime_error("the offscreen frame was skipped");
  r1ui::widgets::image::Image img;
  img.width = kW + 2 * kPad;
  img.height = kH + 2 * kPad;
  img.rgba = target.readPixels();
  const std::string out = (r1test::artifactDir() / (std::string("thumbnail-grid-golden-") + name + ".png")).string();
  r1ui::widgets::image::writePng(out, img.width, img.height, img.rgba);
  std::printf("thumbnail grid golden (%s): %s\n", name, out.c_str());

  const Probe p{img};
  const auto token = [&](const char* n) { return *rig.services.theme().color(n); };
  const layout::Rect box = ui.absRect(grid.id());
  const thumbs::Metrics m = grid.metrics();
  const auto tileOf = [&](size_t i) {
    const thumbs::Rect v = grid.itemViewRect(i);
    return thumbs::Rect{box.x + v.x, box.y + v.y, v.w, v.h};
  };

  // ---- tile fills: selected (grid not focused: the muted fill) and plain (the panel behind) ----
  for (size_t i = 0; i < 14; ++i) {
    const thumbs::Rect t = tileOf(i);
    if (t.y + t.h > box.y + box.h) continue;
    const bool selected = i == 2 || i == 6;
    const int x = static_cast<int>(t.x) + 2;
    const int y = static_cast<int>(t.y + t.h / 2);
    R1_EXPECT(p.near(x, y, selected ? token("panel-selected-muted") : token("panel"), 2));
  }
  // The gap between two tiles of a row is the plain panel.
  {
    const thumbs::Rect a = tileOf(0);
    R1_EXPECT(p.near(static_cast<int>(a.x + a.w + m.gap / 2), static_cast<int>(a.y + a.h / 2), token("panel"), 2));
  }

  // ---- thumbnail square, type strip, modified marker ----
  const auto thumbOf = [&](size_t i) { return thumbs::thumbRect(m, tileOf(i)); };
  {
    const thumbs::Rect th = thumbOf(2);
    R1_EXPECT(p.near(static_cast<int>(th.x) + 6, static_cast<int>(th.y) + 6, token("panel-field"), 2));
    const uint8_t* strip = p.at(static_cast<int>(th.x + th.w / 2), static_cast<int>(th.y + th.h) - 2);
    R1_EXPECT(std::abs(strip[0] - 51) <= 3 && std::abs(strip[1] - 153) <= 3 && std::abs(strip[2] - 77) <= 3);
    const thumbs::Rect marked = thumbOf(3);
    R1_EXPECT(p.near(static_cast<int>(marked.x + marked.w - 4 - 3.5), static_cast<int>(marked.y + 4 + 3.5), token("warning-text"), 4));
    const thumbs::Rect plain = thumbOf(2);
    R1_EXPECT(!p.near(static_cast<int>(plain.x + plain.w - 4 - 3.5), static_cast<int>(plain.y + 4 + 3.5), token("warning-text"), 4));
  }

  // ---- the picture: red near the top, blue near the bottom, inside the thumbnail square ----
  {
    const thumbs::Rect th = thumbOf(4);
    const uint8_t* top = p.at(static_cast<int>(th.x + th.w / 2), static_cast<int>(th.y) + 10);
    const uint8_t* bottom = p.at(static_cast<int>(th.x + th.w / 2), static_cast<int>(th.y + th.h) - 12);
    R1_EXPECT(top[0] > 190 && top[2] < 70);
    R1_EXPECT(bottom[2] > 190 && bottom[0] < 70);
    const uint8_t* strip = p.at(static_cast<int>(th.x + th.w / 2), static_cast<int>(th.y + th.h) - 2);  // the type strip stays over the picture
    R1_EXPECT(std::abs(strip[0] - 51) <= 3 && std::abs(strip[1] - 153) <= 3 && std::abs(strip[2] - 77) <= 3);
  }

  // ---- the icon on a plain tile and ink in the label ----
  {
    const thumbs::Rect th = thumbOf(2);
    int inked = 0;
    for (int y = static_cast<int>(th.y + th.h / 2) - 10; y < static_cast<int>(th.y + th.h / 2) + 10; ++y) {
      for (int x = static_cast<int>(th.x + th.w / 2) - 10; x < static_cast<int>(th.x + th.w / 2) + 10; ++x) inked += p.near(x, y, token("panel-field"), 6) ? 0 : 1;
    }
    R1_EXPECT(inked > 20);  // the muted image icon
    for (const size_t i : {size_t{0}, size_t{1}, size_t{5}}) {
      const thumbs::Rect lr = thumbs::labelRect(m, tileOf(i));
      if (lr.y + lr.h > box.y + box.h) continue;
      int ink = 0;
      for (int y = static_cast<int>(lr.y); y < static_cast<int>(lr.y + lr.h); ++y) {
        for (int x = static_cast<int>(lr.x); x < static_cast<int>(lr.x + lr.w); ++x) {
          const bool selected = i == 2 || i == 6;
          ink += p.near(x, y, selected ? token("panel-selected-muted") : token("panel"), 8) ? 0 : 1;
        }
      }
      R1_EXPECT(ink > 40);
    }
  }

  // ---- the scrollbar thumb at the top of the track: 14 items need more than one screen ----
  R1_EXPECT(m.contentHeight > box.h);
  R1_EXPECT(p.near(static_cast<int>(box.x + box.w - 2 - 3), static_cast<int>(box.y) + 2 + 6, token("border"), 3));
}

}  // namespace

int main() {
  checkTheme(r1ui::theme::ThemeId::Dark, "dark");
  checkTheme(r1ui::theme::ThemeId::Light, "light");
  return r1test::finish();
}
