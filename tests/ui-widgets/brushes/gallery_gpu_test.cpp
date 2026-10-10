// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the GPU render of the brush library in both themes (PNGs under R1UI_ARTIFACT_DIR for the eye):
//   the gallery page (every state of the popup, icons only) and the live path: the library opened by the
//   key over a viewport with procedural pictures delivered through the thumbnail machinery, in four states
//   (opened, S typed, CL typed, the Assign letter popover). Checks: the renders are not empty and differ
//   between themes and states, the pictures really reached the GPU (the cache holds them and the tiles show
//   pixels the icon cannot produce), the accent colour marks the active brush and the next letter, and
//   the validation layer (Debug trees) reports nothing.
// Callers: CTest (label gpu, offscreen, no window).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

#include "TestSupport.h"
#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/CommandRouter.h"
#include "r1ui/commands/Keymap.h"
#include "r1ui/commands/Overrides.h"
#include "r1ui/render/OffscreenTarget.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/widgets/brushes/BrushLibraryController.h"
#include "r1ui/widgets/brushes/GalleryBrushes.h"
#include "r1ui/widgets/commands/CommandKeys.h"
#include "r1ui/widgets/gpu/GpuTextures.h"
#include "r1ui/widgets/image/Png.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/section/Section.h"
#include "r1ui/widgets/thumbnailgrid/GpuThumbnailTextures.h"

namespace {

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;
namespace br = r1ui::commands::brushes;
using r1ui::core::events::Key;

struct Rig {
  Rig() : factory(device), services(r1test::loadTokens(), factory, r1test::assetPaths()) {}
  r1ui::render::RenderDevice device;
  GpuTextureFactory factory;
  Services services;
};

// A 64 x 64 picture: a hue chosen by the key, a bright disc whose size depends on it.
class PaintProvider final : public thumbs::ThumbnailProvider {
 public:
  thumbs::ThumbnailStatus produce(uint64_t key, uint32_t, thumbs::ThumbnailImage& out) override {
    out.width = out.height = 64;
    out.rgba.resize(64 * 64 * 4);
    const double hue = static_cast<double>(key % 360) / 360.0;
    const double radius = 10.0 + static_cast<double>(key % 17);
    for (uint32_t y = 0; y < 64; ++y) {
      for (uint32_t x = 0; x < 64; ++x) {
        const double d = std::hypot(static_cast<double>(x) - 32.0, static_cast<double>(y) - 32.0);
        const double v = std::clamp(1.0 - d / radius, 0.0, 1.0);
        uint8_t* p = &out.rgba[(static_cast<size_t>(y) * 64 + x) * 4];
        p[0] = static_cast<uint8_t>(40 + 180 * v * (0.5 + 0.5 * std::sin(hue * 6.28)));
        p[1] = static_cast<uint8_t>(40 + 180 * v * (0.5 + 0.5 * std::sin(hue * 6.28 + 2.1)));
        p[2] = static_cast<uint8_t>(60 + 170 * v * (0.5 + 0.5 * std::sin(hue * 6.28 + 4.2)));
        p[3] = 255;
      }
    }
    return thumbs::ThumbnailStatus::Ready;
  }
};

r1ui::widgets::image::Image render(Rig& rig, UiContext& ui, int w, int h, int frames) {
  r1ui::render::OffscreenTarget target(rig.device, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
  const r1ui::render::Color clear = rig.services.color("canvas");
  for (int i = 0; i < frames; ++i) {
    ui.setTime(1000 + static_cast<uint64_t>(i) * 16);
    if (!renderFrame(ui, target, clear)) throw std::runtime_error("the offscreen frame was skipped");
  }
  r1ui::widgets::image::Image img;
  img.width = static_cast<uint32_t>(w);
  img.height = static_cast<uint32_t>(h);
  img.rgba = target.readPixels();
  return img;
}

void save(const r1ui::widgets::image::Image& img, const std::string& name) {
  r1ui::widgets::image::writePng((r1test::artifactDir() / (name + ".png")).string(), img.width, img.height, img.rgba);
}

struct Px {
  int r, g, b;
};
Px pixelAt(const r1ui::widgets::image::Image& img, int x, int y) {
  const size_t i = (static_cast<size_t>(y) * img.width + static_cast<size_t>(x)) * 4;
  return {img.rgba[i], img.rgba[i + 1], img.rgba[i + 2]};
}
bool near(const Px& p, const Px& q, int tol) { return std::abs(p.r - q.r) <= tol && std::abs(p.g - q.g) <= tol && std::abs(p.b - q.b) <= tol; }

// Counts pixels of a rectangle that are close to a colour.
int countNear(const r1ui::widgets::image::Image& img, const layout::RectD& r, const Px& c, int tol) {
  int n = 0;
  for (int y = static_cast<int>(r.y); y < static_cast<int>(r.y + r.h); ++y) {
    for (int x = static_cast<int>(r.x); x < static_cast<int>(r.x + r.w); ++x) n += near(pixelAt(img, x, y), c, tol) ? 1 : 0;
  }
  return n;
}

std::vector<br::BrushInfo> sampleBrushes() {
  const std::pair<const char*, const char*> names[] = {
      {"Standard", "Sculpt"}, {"Smooth", "Smooth"}, {"Snake Hook", "Move"}, {"Slash", "Cut"},   {"Clay Buildup", "Sculpt"}, {"Clay", "Sculpt"},    {"Inflate", "Sculpt"},
      {"Pinch", "Sculpt"},    {"Move", "Move"},     {"Blob", "Sculpt"},     {"Flatten", "Surface"}, {"Polish", "Surface"},    {"Hpolish", "Surface"}, {"Crease", "Sculpt"},
      {"Dam Standard", "Sculpt"}, {"Layer", "Surface"}, {"Trim", "Cut"},    {"Mask Pen", "Mask"},   {"Nudge", "Move"},        {"Orb Cracks", "Surface"}};
  std::vector<br::BrushInfo> list;
  for (const auto& [name, category] : names) {
    br::BrushInfo info;
    info.name = name;
    for (const char* c = name; *c != 0; ++c) info.id += *c == ' ' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
    info.category = category;
    info.icon = "palette";
    list.push_back(std::move(info));
  }
  return list;
}

// One theme: the gallery, then the live library in four states.
void checkTheme(r1ui::theme::ThemeId theme, const char* name, r1ui::widgets::image::Image* galleryOut, r1ui::widgets::image::Image liveOut[4]) {
  // ---- gallery ----
  {
    Rig rig;
    rig.services.theme().set(theme);
    UiContext ui(rig.services);
    ui.setAnimationsEnabled(false);
    ui.setViewport(1300, 1960, 1.0f);
    ui.rootStyle().direction = layout::FlexDirection::Column;
    ui.rootStyle().alignItems = layout::Align::Start;
    for (double& p : ui.rootStyle().padding) p = 16.0;
    buildGalleryBrushes(ui, ui.root());
    ui.frame();
    *galleryOut = render(rig, ui, 1300, 1960, 2);
    save(*galleryOut, std::string("brush-gallery-") + name);
  }

  // ---- live: the key, the popup, pictures ----
  Rig rig;
  rig.services.theme().set(theme);
  PaintProvider provider;
  GpuThumbnailTextures sink(rig.device);
  r1ui::commands::CommandRegistry registry;
  r1ui::commands::KeybindingOverrides overrides{registry};
  r1ui::commands::Keymap keymap{registry, overrides};
  br::BrushLibraryModel model;
  model.setBrushes(sampleBrushes());
  model.setFavourite("smooth", true);
  model.setFavourite("pinch", true);
  model.noteUsed("trim");
  model.noteUsed("blob");
  model.noteUsed("clay");
  model.setActiveId("clay");
  UiContext ui(rig.services);  // after the sink: the popup's pictures are released before the sink goes
  ui.setAnimationsEnabled(false);
  ui.setViewport(900, 620, 1.0f);
  UiClock clock(ui);
  r1ui::commands::CommandRouter router(registry, keymap, clock);
  CommandServices services{registry, overrides, keymap, router};
  Label& canvas = ui.create<Label>(ui.root(), "Viewport", LabelRole::Muted);
  canvas.style().margin[layout::kLeft] = layout::Length::px(40.0);
  CommandKeyHandler keys(ui, services);
  std::unique_ptr<BrushLibraryController> controller;
  controller = std::make_unique<BrushLibraryController>(ui, services, model, [&](const std::string& id) { model.setActiveId(id); });
  controller->setThumbnails(&provider, &sink);
  auto tap = controller->tapKeys(ui, &keys);
  ui.setGlobalKeyHandler(tap.get());
  ui.frame();

  const auto type = [&](char c) {
    ui.keyDown(static_cast<Key>(std::toupper(c)), 0);
    ui.textInput(static_cast<char32_t>(c), 0);
    ui.keyUp(static_cast<Key>(std::toupper(c)), 0);
  };
  ui.pointerMove(450, 310);
  type('b');
  R1_EXPECT(controller->isOpen());
  ui.frame();
  BrushLibraryPopup* popup = controller->popup();
  if (popup == nullptr) throw std::runtime_error("the library did not open");
  liveOut[0] = render(rig, ui, 900, 620, 6);
  save(liveOut[0], std::string("brush-live-open-") + name);
  R1_EXPECT(controller->thumbnails()->cached() > 10);

  // The active brush's tile carries the accent outline; the pictures are not the icon grey.
  const Px accent = theme == r1ui::theme::ThemeId::Dark ? Px{59, 130, 246} : Px{37, 99, 235};
  int activeTile = -1;
  for (size_t t = popup->result().recentCount; t < popup->result().tiles.size(); ++t) {
    if (popup->result().tiles[t].active) activeTile = static_cast<int>(t);
  }
  R1_EXPECT(activeTile >= 0);
  if (activeTile >= 0) {
    const auto r = popup->tileRect(static_cast<size_t>(activeTile));
    R1_EXPECT(r.w > 0.0);
    R1_EXPECT(countNear(liveOut[0], {r.x, r.y, r.w, 3.0}, accent, 24) > 40);   // the outline's top edge
  }
  {
    const auto r = popup->tileRect(static_cast<size_t>(popup->result().recentCount) + 1);  // a plain tile: its picture area is not flat
    const Px corner = pixelAt(liveOut[0], static_cast<int>(r.x + r.w / 2), static_cast<int>(r.y + 36));
    const Px edge = pixelAt(liveOut[0], static_cast<int>(r.x + 14), static_cast<int>(r.y + 8));
    R1_EXPECT(!near(corner, edge, 8));
  }

  type('s');
  ui.frame();
  liveOut[1] = render(rig, ui, 900, 620, 3);
  save(liveOut[1], std::string("brush-live-typed-s-") + name);
  R1_EXPECT(popup->result().matchCount == 4);
  // The marked next letters are accent filled pills.
  {
    const auto r = popup->tileRect(0);
    R1_EXPECT(countNear(liveOut[1], {r.x + 4.0, r.y + 6.0, 48.0, 16.0}, accent, 24) > 20);
  }

  type('c');   // "sc" matches nothing... go back and type another prefix
  ui.keyDown(Key::Backspace, 0);
  ui.keyUp(Key::Backspace, 0);
  ui.keyDown(Key::Backspace, 0);
  ui.keyUp(Key::Backspace, 0);
  type('c');
  type('l');
  ui.frame();
  liveOut[2] = render(rig, ui, 900, 620, 3);
  save(liveOut[2], std::string("brush-live-typed-cl-") + name);
  R1_EXPECT(popup->result().matchCount == 2);

  ui.keyDown(Key::Backspace, 0);
  ui.keyUp(Key::Backspace, 0);
  ui.keyDown(Key::Backspace, 0);
  ui.keyUp(Key::Backspace, 0);
  ui.keyDown(static_cast<Key>(113), 0);  // F2
  ui.keyUp(static_cast<Key>(113), 0);
  type('s');
  ui.frame();
  liveOut[3] = render(rig, ui, 900, 620, 3);
  save(liveOut[3], std::string("brush-live-assign-") + name);
  R1_EXPECT(popup->assigning());
  controller->close();
  ui.setGlobalKeyHandler(nullptr);
}

// The time from the B key to the first drawn frame with 2000 brushes: the key through the router, the command,
// the popup, the query, the layout and one real frame on the device (offscreen, fence wait included). The
// first opening of the session (cold: fonts, glyphs, pipelines) is reported apart from the later ones.
void measureOpenLatency() {
  Rig rig;
  PaintProvider provider;
  GpuThumbnailTextures sink(rig.device);
  r1ui::commands::CommandRegistry registry;
  r1ui::commands::KeybindingOverrides overrides{registry};
  r1ui::commands::Keymap keymap{registry, overrides};
  br::BrushLibraryModel model;
  std::vector<br::BrushInfo> list;
  uint64_t state = 4242;
  static const char* const syllables[] = {"ca", "ma", "so", "ti", "ro", "ne", "lu", "pa", "ki", "do", "fe", "gu"};
  for (int i = 0; i < 2000; ++i) {
    br::BrushInfo info;
    for (int p = 0; p < 3; ++p) {
      state = state * 6364136223846793005ull + 1442695040888963407ull;
      info.name += syllables[(state >> 33) % 12];
    }
    info.name += " " + std::to_string(i);
    info.id = "m" + std::to_string(i);
    info.category = "Cat" + std::to_string(i % 6);
    info.icon = "palette";
    list.push_back(std::move(info));
  }
  model.setBrushes(std::move(list));
  UiContext ui(rig.services);
  ui.setAnimationsEnabled(false);
  ui.setViewport(900, 620, 1.0f);
  UiClock clock(ui);
  r1ui::commands::CommandRouter router(registry, keymap, clock);
  CommandServices services{registry, overrides, keymap, router};
  Label& canvas = ui.create<Label>(ui.root(), "Viewport", LabelRole::Muted);
  canvas.style().margin[layout::kLeft] = layout::Length::px(40.0);
  CommandKeyHandler keys(ui, services);
  BrushLibraryController controller(ui, services, model, [&](const std::string& id) { model.setActiveId(id); });
  controller.setThumbnails(&provider, &sink);
  auto tap = controller.tapKeys(ui, &keys);
  ui.setGlobalKeyHandler(tap.get());
  ui.frame();
  r1ui::render::OffscreenTarget target(rig.device, 900, 620);
  const r1ui::render::Color clear = rig.services.color("canvas");
  for (int i = 0; i < 2; ++i) renderFrame(ui, target, clear);  // the renderer's own first frames (pipelines) are not the library's cost
  using Clock = std::chrono::steady_clock;
  std::vector<double> times;
  double coldOpenMs = 0.0;
  for (int round = 0; round < 8; ++round) {
    ui.pointerMove(450, 310);
    const auto begin = Clock::now();
    ui.keyDown(static_cast<Key>('B'), 0);
    ui.textInput(U'b', 0);
    ui.keyUp(static_cast<Key>('B'), 0);
    const auto opened = Clock::now();
    if (!renderFrame(ui, target, clear)) throw std::runtime_error("the offscreen frame was skipped");
    times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
    if (round == 0) coldOpenMs = std::chrono::duration<double, std::milli>(opened - begin).count();
    R1_EXPECT(controller.isOpen());
    for (int i = 0; i < 4; ++i) renderFrame(ui, target, clear);  // the pictures arrive
    controller.close();
    renderFrame(ui, target, clear);
  }
  std::vector<double> warm(times.begin() + 1, times.end());
  std::sort(warm.begin(), warm.end());
  std::printf("brush library, 2000 brushes: key to first drawn frame (offscreen): cold %.2f ms (key and popup %.2f ms, frame %.2f ms), warm median %.2f ms, worst %.2f ms\n", times.front(), coldOpenMs,
              times.front() - coldOpenMs, warm[warm.size() / 2], warm.back());
  R1_EXPECT(warm[warm.size() / 2] < 16.0);
  ui.setGlobalKeyHandler(nullptr);
}

}  // namespace

int main() {
  measureOpenLatency();
  r1ui::widgets::image::Image gallery[2];
  r1ui::widgets::image::Image live[2][4];
  checkTheme(r1ui::theme::ThemeId::Dark, "dark", &gallery[0], live[0]);
  checkTheme(r1ui::theme::ThemeId::Light, "light", &gallery[1], live[1]);
  R1_EXPECT(gallery[0].rgba != gallery[1].rgba);
  for (int s = 0; s < 4; ++s) {
    R1_EXPECT(live[0][s].rgba != live[1][s].rgba);
    for (int u = s + 1; u < 4; ++u) R1_EXPECT(live[0][s].rgba != live[0][u].rgba);
  }
  return r1test::finish();
}
