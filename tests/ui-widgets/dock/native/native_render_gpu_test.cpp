// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: two renders of the native floating window's own content. (1) Offscreen, through the visual
//   harness: the toolkit-drawn frame (title bar, title text, maximize and close buttons, border) and
//   its content holder in both themes; checked for the title bar and body colours, ink in the title
//   text and the button icons, and written as PNGs. (2) In a real native window: the content of a
//   floating window drawn through its swapchain, read back with PrintWindow and checked for the same
//   colours, with the content widget's solid colour in the content rectangle.
// Why: slice 5.2 asks for a GPU render of a floating window's content; the second part also proves
//   that the per-window swapchain, context and atlas consumer produce a picture and that the content
//   rectangle the backend reports is where the content is drawn.
// Callers: CTest (label gpu). The real-window part skips itself (the offscreen part still runs)
//   without an interactive desktop. PNGs go to R1UI_ARTIFACT_DIR.
#include <windows.h>

#include <cmath>
#include <set>

#include "ExpectWithMessage.h"
#include "NativeRig.h"
#include "VisualSupport.h"
#include "r1ui/widgets/image/Png.h"
#include "r1ui/widgets/testing/VisualHarness.h"
#include "../../../../source/ui-widgets/src/gpu/dock/NativeFrame.h"

using namespace native_test;
namespace image = r1ui::widgets::image;

namespace {

// A solid colour filling its parent, so the content rectangle is visible in a capture.
class Swatch : public WidgetObject {
 public:
  explicit Swatch(r1ui::render::Color color) : color_(color) {}
  const char* typeName() const override { return "Swatch"; }
  void onAttached() override {
    style().width = r1ui::core::layout::Length::percent(100);
    style().height = r1ui::core::layout::Length::percent(100);
  }
  void paint(PaintContext& ctx) override { ctx.painter().fillRect(ctx.box(), color_); }

 private:
  r1ui::render::Color color_;
};

struct Px {
  int r = 0, g = 0, b = 0;
};

Px pixel(const image::Image& img, int x, int y) {
  const size_t i = (static_cast<size_t>(y) * img.width + static_cast<size_t>(x)) * 4;
  return {img.rgba[i], img.rgba[i + 1], img.rgba[i + 2]};
}

bool close(Px a, r1ui::render::Color c, int tolerance = 3) {
  const auto to8 = [](float v) { return static_cast<int>(std::lround(v * 255.0f)); };
  return std::abs(a.r - to8(c.r)) <= tolerance && std::abs(a.g - to8(c.g)) <= tolerance && std::abs(a.b - to8(c.b)) <= tolerance;
}

size_t distinctColours(const image::Image& img, int x0, int y0, int x1, int y1) {
  std::set<uint32_t> colours;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const Px p = pixel(img, x, y);
      colours.insert(static_cast<uint32_t>(p.r << 16 | p.g << 8 | p.b));
    }
  }
  return colours.size();
}

void offscreen_frame() {
  using r1ui::theme::ThemeId;
  const auto paths = r1test::visual::paths();
  r1ui::widgets::Services& services = r1ui::widgets::testing::sharedServices(paths);
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    r1ui::widgets::testing::RenderSpec spec;
    spec.width = 360;
    spec.height = 220;
    spec.theme = theme;
    spec.padding = 0;
    const image::Image img = r1ui::widgets::testing::renderWidget(
        [](UiContext& ui, r1ui::core::tree::WidgetId parent) {
          auto& frame = ui.create<native::NativeFrame>(parent, native::FrameMetrics{});
          frame.setTitle("Outliner");
          ui.create<native::NativeHolder>(frame.id());
          return frame.id();
        },
        spec, paths);
    const char* name = theme == ThemeId::Dark ? "dark" : "light";
    image::writePng(paths.artifactDir / (std::string("native-frame-") + name + ".png"), img.width, img.height, img.rgba);
    // The theme's own colours: title bar = panel-secondary, body = panel.
    const r1ui::render::Color titleBar = services.color("panel-secondary");
    const r1ui::render::Color body = services.color("panel");
    R1_EXPECT(close(pixel(img, 200, 10), titleBar), "the title bar is drawn in the title colour");
    R1_EXPECT(close(pixel(img, 200, 120), body), "the body in the panel colour");
    R1_EXPECT(distinctColours(img, 8, 6, 120, 28) > 6, "the title text has ink");
    R1_EXPECT(distinctColours(img, 360 - 64, 4, 360 - 6, 30) > 4, "so do the maximize and close icons");
    R1_EXPECT(!close(pixel(img, 0, 100), body, 1) || !close(pixel(img, 359, 100), body, 1), "the 1 px border differs from the body");
    std::printf("  native-frame-%s.png: %u x %u\n", name, img.width, img.height);
  }
}

// Reads a window's pixels with PrintWindow (the compositor's copy of what is shown).
bool capture(HWND hwnd, image::Image& out) {
  RECT r{};
  GetWindowRect(hwnd, &r);
  const int w = r.right - r.left;
  const int h = r.bottom - r.top;
  if (w <= 0 || h <= 0) return false;
  HDC screen = GetDC(nullptr);
  HDC dc = CreateCompatibleDC(screen);
  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -h;  // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(dc, bitmap);
  const BOOL ok = PrintWindow(hwnd, dc, 2);  // PW_RENDERFULLCONTENT: includes the GPU swapchain
  out.width = static_cast<uint32_t>(w);
  out.height = static_cast<uint32_t>(h);
  out.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  const auto* src = static_cast<const uint8_t*>(bits);
  for (size_t i = 0; i < static_cast<size_t>(w) * static_cast<size_t>(h); ++i) {
    out.rgba[i * 4 + 0] = src[i * 4 + 2];
    out.rgba[i * 4 + 1] = src[i * 4 + 1];
    out.rgba[i * 4 + 2] = src[i * 4 + 0];
    out.rgba[i * 4 + 3] = 255;
  }
  SelectObject(dc, old);
  DeleteObject(bitmap);
  DeleteDC(dc);
  ReleaseDC(nullptr, screen);
  return ok != FALSE;
}

void real_window_content() {
  std::string skip;
  auto rig = NativeRig::create(skip);
  if (!rig) {
    std::printf("  real-window part SKIPPED: %s\n", skip.c_str());
    return;
  }
  NativeFloatingBackend& b = *rig->backend;
  const dock::Point p = rig->pointInMain();
  FloatRequest request;
  request.panels = {1};
  request.active = 1;
  request.title = "Inspector";
  request.contentRect = {p.x - 100, p.y - 60, 320, 200};
  const FloatCreateResult made = b.createWindow(request);
  R1_EXPECT(made.ok);
  const std::optional<FloatContent> content = b.content(made.id);
  const r1ui::render::Color swatch = r1ui::render::Color::fromRgba8(200, 60, 40);
  content->ui->create<Swatch>(content->parent, swatch);
  rig->settle(6);
  Sleep(300);  // the compositor needs a moment to pick up the first presented frame
  rig->settle(2);
  R1_EXPECT(b.framesPresented(made.id) > 0, "the window drew frames through its own swapchain");
  image::Image img;
  HWND hwnd = hwndOf(b.nativeWindow(made.id));
  R1_EXPECT(capture(hwnd, img), "PrintWindow succeeded");
  if (img.width == 0) return;
  image::writePng(r1test::artifactDir() / "native-window-capture.png", img.width, img.height, img.rgba);
  const r1ui::render::Color titleBar = rig->services->color("panel-secondary");
  // Window pixel (x, y) = client (x, y): the window is borderless. The content rectangle starts 1 px
  // in and 34 px down.
  R1_EXPECT(close(pixel(img, 200, 10), titleBar), "title bar colour");
  R1_EXPECT(close(pixel(img, 60, 34 + 40), swatch), "the content widget fills the content rectangle");
  R1_EXPECT(close(pixel(img, 1 + 2, 34 + 2), swatch), "right at the content rectangle's top-left");
  R1_EXPECT(close(pixel(img, static_cast<int>(img.width) - 1 - 3, static_cast<int>(img.height) - 1 - 3), swatch), "and at its bottom-right");
  R1_EXPECT(distinctColours(img, 10, 6, 130, 28) > 6, "the title text is drawn");
  const dock::Rect rect = *b.contentRect(made.id);
  R1_EXPECT(std::abs(static_cast<double>(img.width) - (rect.w + 2)) <= 1.5 && std::abs(static_cast<double>(img.height) - (rect.h + 35)) <= 1.5, "the window is the content rectangle plus the frame");
  std::printf("  native-window-capture.png: %u x %u\n", img.width, img.height);
  R1_EXPECT(rig->device->validationMessageCount() == 0, "no validation-layer message (active in Debug trees)");
}

}  // namespace

int main() try {
  offscreen_frame();
  real_window_content();
  R1_EXPECT(r1ui::widgets::testing::validationMessageCount() == 0);
  return r1test::finish();
} catch (const std::exception& e) {
  std::fprintf(stderr, "uncaught exception: %s\n", e.what());
  return 2;
}
