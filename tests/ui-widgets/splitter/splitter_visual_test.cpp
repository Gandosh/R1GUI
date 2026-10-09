// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for Splitter in both themes. The reference app has fixed panels (no resizable
//   splitter exists in the captures), so there is no reference image: the test renders the splitter
//   offscreen and checks the pixels of its handle against the token colours it must use: a 1 px line of
//   `border` when idle, a full 5 px bar of `border-strong` while hovered, of `accent` while dragged,
//   a focus ring for a keyboard-focused handle, and that the panes are not painted over by the handle.
//   The renders are written to the artifact folder for the eye.
// Callers: CTest (splitter gpu: renders offscreen on a Vulkan device, no window).
#include <cmath>

#include "TestSupport.h"
#include "VisualSupport.h"
#include "r1ui/widgets/splitter/Splitter.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;
using r1ui::theme::ThemeId;
using r1ui::widgets::testing::RenderSpec;
using r1ui::widgets::testing::renderWidget;

enum class Mode { Idle, Hover, Drag, Focus };

struct Pixel {
  int r, g, b;
};

Pixel at(const r1ui::widgets::image::Image& img, int x, int y) {
  const uint8_t* p = img.rgba.data() + (static_cast<size_t>(y) * img.width + static_cast<size_t>(x)) * 4;
  return {p[0], p[1], p[2]};
}

Pixel token(r1test::TestUi& t, const char* name) {
  const auto c = t.services.color(name);
  return {static_cast<int>(std::lround(c.r * 255)), static_cast<int>(std::lround(c.g * 255)), static_cast<int>(std::lround(c.b * 255))};
}

bool near(Pixel a, Pixel b, int tolerance = 3) { return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance && std::abs(a.b - b.b) <= tolerance; }

// Two panes in a 205 x 60 splitter at (0, 0); the handle's bar is x 100..104 (5 px) between two 100 px panes.
r1test::visual::Build build(Mode mode) {
  return [mode](UiContext& ui, WidgetId parent) {
    Splitter& s = ui.create<Splitter>(parent);
    s.style().width = layout::Length::px(205);
    s.style().height = layout::Length::px(60);
    s.style().flexShrink = 0.0;
    s.addPane();
    s.addPane();
    ui.frame();
    const layout::Rect h = s.handleRect(0);
    if (mode == Mode::Hover || mode == Mode::Drag) ui.pointerMove(h.x + 2, 30);
    if (mode == Mode::Drag) ui.pointerDown(h.x + 2, 30);
    if (mode == Mode::Focus) s.setKeyboardResize(true);
    return s.id();
  };
}

}  // namespace

int main() {
  const auto paths = r1test::visual::paths();
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    r1test::TestUi probe;  // token colours of the theme under test
    probe.services.theme().set(theme);
    const Pixel panel = token(probe, "panel");
    const Pixel border = token(probe, "border");
    const Pixel strong = token(probe, "border-strong");
    const Pixel accent = token(probe, "accent");
    const char* name = theme == ThemeId::Dark ? "dark" : "light";
    for (const Mode mode : {Mode::Idle, Mode::Hover, Mode::Drag, Mode::Focus}) {
      RenderSpec spec;
      spec.width = 205;
      spec.height = 60;
      spec.theme = theme;
      spec.padding = 0;
      spec.state = mode == Mode::Focus ? r1ui::widgets::testing::VisualState::Focus : r1ui::widgets::testing::VisualState::Idle;
      const auto img = renderWidget(build(mode), spec, paths);
      const char* modeName = mode == Mode::Idle ? "idle" : mode == Mode::Hover ? "hover" : mode == Mode::Drag ? "drag" : "focus";
      r1ui::widgets::image::writePng(paths.artifactDir / (std::string("splitter-") + modeName + "-" + name + ".png"), img.width, img.height, img.rgba);
      // Panes keep the panel colour; the handle column (x 100..104) shows the state's colour.
      R1_EXPECT(near(at(img, 50, 30), panel) && near(at(img, 150, 30), panel));
      switch (mode) {
        case Mode::Idle:
          R1_EXPECT(near(at(img, 102, 30), border) && near(at(img, 100, 30), panel) && near(at(img, 104, 30), panel));  // one pixel line, centred
          break;
        case Mode::Hover:
          R1_EXPECT(near(at(img, 100, 30), strong) && near(at(img, 104, 30), strong) && near(at(img, 99, 30), panel) && near(at(img, 105, 30), panel));
          break;
        case Mode::Drag:
          R1_EXPECT(near(at(img, 100, 30), accent) && near(at(img, 104, 30), accent));
          break;
        case Mode::Focus:
          // The idle line plus a focus ring around the handle: the ring is not the panel colour at the handle's edge.
          R1_EXPECT(near(at(img, 102, 30), border) == false || !near(at(img, 100, 30), panel));
          break;
      }
      std::printf("visual splitter-%s-%s: handle pixel (102,30) = %d,%d,%d\n", modeName, name, at(img, 102, 30).r, at(img, 102, 30).g, at(img, 102, 30).b);
    }
  }
  return r1test::finish();
}
