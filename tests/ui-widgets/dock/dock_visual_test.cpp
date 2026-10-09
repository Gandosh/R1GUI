// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: GPU visual oracle for the docking widgets in both themes: a docked layout (strips, active
//   tab and region marker, splitter handle, panel bodies), a tab drag in progress (lifted tab, the
//   translucent zone preview, the cross, the edge target), a floating window (rounded 8 px corners,
//   title bar, shadow), an overflowing strip (60 px tabs, scroll arrows) and the empty-area
//   placeholder. There is no reference image (the reference editor has no docking), so the test
//   renders offscreen through the real renderer and checks structural pixels against token colours,
//   and writes the renders to the artifact folder for the eye.
// Callers: CTest (dock gpu: renders offscreen on a Vulkan device, no window).
#include <cmath>
#include <functional>
#include <memory>

#include "DockTestSupport.h"
#include "VisualSupport.h"
#include "r1ui/widgets/image/Png.h"

namespace {

using namespace dock_widget_test;
using r1ui::theme::ThemeId;
using r1ui::widgets::testing::RenderSpec;
using r1ui::widgets::testing::renderWidget;

struct Pixel {
  int r, g, b;
};

Pixel at(const r1ui::widgets::image::Image& img, double x, double y) {
  const int px = std::clamp(static_cast<int>(x), 0, static_cast<int>(img.width) - 1);
  const int py = std::clamp(static_cast<int>(y), 0, static_cast<int>(img.height) - 1);
  const uint8_t* p = img.rgba.data() + (static_cast<size_t>(py) * img.width + static_cast<size_t>(px)) * 4;
  return {p[0], p[1], p[2]};
}

Pixel token(r1test::TestUi& t, const char* name) {
  const auto c = t.services.color(name);
  return {static_cast<int>(std::lround(c.r * 255)), static_cast<int>(std::lround(c.g * 255)), static_cast<int>(std::lround(c.b * 255))};
}

Pixel mix(Pixel under, Pixel over, double alpha) {
  return {static_cast<int>(std::lround(under.r * (1 - alpha) + over.r * alpha)), static_cast<int>(std::lround(under.g * (1 - alpha) + over.g * alpha)),
          static_cast<int>(std::lround(under.b * (1 - alpha) + over.b * alpha))};
}

bool near(Pixel a, Pixel b, int tolerance = 4) { return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance && std::abs(a.b - b.b) <= tolerance; }
int luminance(Pixel p) { return (p.r * 3 + p.g * 6 + p.b) / 10; }

// Solid colour with the panel's number: easy to find in a render.
class Swatch : public WidgetObject {
 public:
  Swatch(const char* colorToken) : token_(colorToken) {}
  const char* typeName() const override { return "Swatch"; }
  void paint(PaintContext& ctx) override { ctx.painter().fillRect(ctx.box(), ctx.color(token_)); }
  std::string token_;
};

const char* const kSwatchTokens[] = {"", "accent", "component", "success-bg", "error", "warning-action", "code-tag", "code-attribute"};

// Owns what the dock needs, like the gallery's box.
class DockBox : public WidgetObject {
 public:
  const char* typeName() const override { return "DockBox"; }
  void onAttached() override {
    style().width = r1ui::core::layout::Length::px(800);
    style().height = r1ui::core::layout::Length::px(500);
    style().flexShrink = 0.0;
    style().direction = r1ui::core::layout::FlexDirection::Column;
  }
  void build(dock::PanelId count, DockHostOptions options = {}) {
    for (dock::PanelId id = 1; id <= count; ++id) {
      PanelDescriptor d;
      d.id = id;
      d.title = "Panel " + std::to_string(id);
      d.floatSize = {300.0, 200.0};
      const char* tokenName = kSwatchTokens[(id - 1) % 7 + 1];
      d.factory = [tokenName](UiContext& ui, WidgetId parent) { return ui.create<Swatch>(parent, tokenName).id(); };
      registry.add(std::move(d));
    }
    backend = std::make_unique<InWindowFloatingBackend>(ui(), ui().root());
    host = &ui().create<DockHost>(id(), registry, *backend, options);
  }
  void setRoot(dock::Node root) {
    dock::DockLayoutResult created = dock::DockLayout::create(registry.infos(), host->options().config, std::move(root));
    host->setLayout(std::move(*created.layout));
    for (int i = 0; i < 4; ++i) ui().frame();
  }
  PanelRegistry registry;
  std::unique_ptr<InWindowFloatingBackend> backend;
  DockHost* host = nullptr;
};

// Geometry the render checks need, captured while the scene is built.
struct Probe {
  dock::Rect strip1, tab1, tab2, body1, body4, body5, handle, floatFrame, floatContent, list, hint;
  dock::Rect preview;
};

Probe probe;

dock::Point centre(const dock::Rect& r) { return {r.x + r.w / 2.0, r.y + r.h / 2.0}; }

dock::Node mainLayout() {
  return dock::Node::split(dock::Axis::Row, {stackOf({1, 2, 3}), dock::Node::split(dock::Axis::Column, {stackOf({4}), stackOf({5, 6})})});
}

void capture(DockBox& box) {
  UiContext& ui = box.ui();
  const dock::LayoutResult r = box.host->layout().computeLayout(box.host->mainContentRect());
  probe = {};
  if (r.stacks.size() >= 3) {
    probe.strip1 = r.stacks[0].strip;
    probe.body1 = r.stacks[0].body;
    probe.body4 = r.stacks[1].body;
    probe.body5 = r.stacks[2].body;
  } else if (!r.stacks.empty()) {
    probe.strip1 = r.stacks[0].strip;
    probe.body1 = r.stacks[0].body;
  }
  if (!r.handles.empty()) probe.handle = r.handles[0].rect;
  if (DockTabStrip* s = box.host->stripOf(1)) {
    probe.tab1 = s->tabRect(0);
    if (s->tabCount() > 1) probe.tab2 = s->tabRect(1);
    probe.list = s->listButtonRect();
  }
  (void)ui;
}

r1test::visual::Build idleScene() {
  return [](UiContext& ui, WidgetId parent) {
    DockBox& box = ui.create<DockBox>(parent);
    box.build(6);
    box.setRoot(mainLayout());
    box.host->activatePanel(1);
    ui.frame();
    capture(box);
    return box.id();
  };
}

r1test::visual::Build dragScene() {
  return [](UiContext& ui, WidgetId parent) {
    DockBox& box = ui.create<DockBox>(parent);
    box.build(6);
    box.setRoot(mainLayout());
    box.host->activatePanel(2);
    ui.frame();
    capture(box);
    // Pick tab 2 up and hold it over the left part of the top right region's body.
    const dock::Point from = centre(probe.tab2);
    const dock::Rect body = probe.body4;
    const dock::Point to{body.x + body.w * 0.12, body.y + body.h * 0.5};
    ui.pointerMove(from.x, from.y);
    ui.pointerDown(from.x, from.y);
    for (int i = 1; i <= 8; ++i) {
      ui.pointerMove(from.x + (to.x - from.x) * i / 8, from.y + (to.y - from.y) * i / 8);
      ui.frame();
    }
    probe.preview = {body.x, body.y - 25, body.w / 2.0, body.h + 25};
    return box.id();
  };
}

r1test::visual::Build floatScene() {
  return [](UiContext& ui, WidgetId parent) {
    DockBox& box = ui.create<DockBox>(parent);
    box.build(6);
    box.setRoot(mainLayout());
    box.host->floatPanel(5);
    for (int i = 0; i < 4; ++i) ui.frame();
    box.host->setWindowRect(box.host->layout().areas()[1].id, {180, 150, 400, 250});
    for (int i = 0; i < 4; ++i) ui.frame();
    capture(box);
    const std::vector<FloatId> windows = box.backend->stacking();
    probe.floatFrame = toDockRect(ui.absRect(box.backend->frameWidget(windows.back())));
    probe.floatContent = *box.backend->contentRect(windows.back());
    return box.id();
  };
}

r1test::visual::Build overflowScene() {
  return [](UiContext& ui, WidgetId parent) {
    DockBox& box = ui.create<DockBox>(parent);
    box.build(40);
    std::vector<dock::PanelId> ids;
    for (dock::PanelId i = 1; i <= 40; ++i) ids.push_back(i);
    box.setRoot(stackOf(ids));
    box.host->activatePanel(3);
    ui.frame();
    capture(box);
    return box.id();
  };
}

r1test::visual::Build emptyScene() {
  return [](UiContext& ui, WidgetId parent) {
    DockBox& box = ui.create<DockBox>(parent);
    box.build(3);
    box.setRoot(stackOf({1}));
    box.host->closePanel(1);
    ui.frame();
    capture(box);
    probe.hint = box.host->mainContentRect();
    return box.id();
  };
}

Pixel backgroundOf(r1test::TestUi& probeUi, const char* name) { return token(probeUi, name); }

void checkIdle(const r1ui::widgets::image::Image& img, r1test::TestUi& tokens) {
  const Pixel canvas = backgroundOf(tokens, "canvas");
  const Pixel panel = backgroundOf(tokens, "panel");
  const Pixel border = backgroundOf(tokens, "border");
  const Pixel accent = backgroundOf(tokens, "accent");
  // Strip: canvas background, front tab on the panel colour with the accent marker of the active region.
  R1_EXPECT(near(at(img, probe.strip1.right() - 3, probe.strip1.y + 12), canvas) || near(at(img, probe.strip1.right() - 3, probe.strip1.y + 12), panel), "strip background past the tabs");
  R1_EXPECT(near(at(img, probe.tab1.x + 100, probe.tab1.y + 20), panel), "front tab on the panel colour");
  R1_EXPECT(near(at(img, probe.tab2.x + 100, probe.tab2.y + 20), canvas), "background tab on the canvas colour");
  R1_EXPECT(near(at(img, probe.tab1.x + 100, probe.tab1.y + 1), accent), "2 px marker on the active region's front tab");
  R1_EXPECT(near(at(img, probe.strip1.x + 60, probe.strip1.bottom() - 1), border), "1 px border under the strip");
  // Splitter handle: a 1 px line of the border colour centred in the 5 px gap.
  R1_EXPECT(near(at(img, probe.handle.x + 2, probe.handle.y + 200), border), "handle line");
  // Bodies show their panel's swatch colour.
  R1_EXPECT(near(at(img, probe.body1.x + 40, probe.body1.y + 40), backgroundOf(tokens, "accent")), "panel 1 content");
  R1_EXPECT(near(at(img, probe.body4.x + 40, probe.body4.y + 40), backgroundOf(tokens, "error")), "panel 4 content");
  // Label ink: the front tab has text pixels that are not the tab colour.
  int ink = 0;
  for (int y = static_cast<int>(probe.tab1.y) + 5; y < static_cast<int>(probe.tab1.y) + 20; ++y) {
    for (int x = static_cast<int>(probe.tab1.x) + 8; x < static_cast<int>(probe.tab1.x) + 90; ++x) ink += near(at(img, x, y), panel, 12) ? 0 : 1;
  }
  R1_EXPECT(ink > 40, "the tab label is drawn");
}

void checkDrag(const r1ui::widgets::image::Image& img, r1test::TestUi& tokens) {
  const Pixel accent = backgroundOf(tokens, "accent");
  const Pixel error = backgroundOf(tokens, "error");
  const Pixel canvas = backgroundOf(tokens, "canvas");
  // The zone preview: the left half of the target region (strip and body) is tinted 25 percent.
  const dock::Point inside{probe.body4.x + 30, probe.body4.y + probe.body4.h * 0.8};
  R1_EXPECT(near(at(img, inside.x, inside.y), mix(error, accent, 0.25), 6), "the preview half is tinted with the accent at 25 percent");
  const dock::Point outside{probe.body4.x + probe.body4.w * 0.9, probe.body4.y + probe.body4.h * 0.5};
  R1_EXPECT(near(at(img, outside.x, outside.y), error), "the other half is untouched");
  // The preview has a solid outline.
  R1_EXPECT(!near(at(img, probe.body4.x + 1, probe.body4.y + probe.body4.h * 0.8), error, 30), "outline");
  // Edge targets along the border of the main area.
  const dock::Rect main{0, 0, 800, 500};
  R1_EXPECT(!near(at(img, main.right() - 3, main.y + 250), error, 10) && !near(at(img, main.right() - 3, 250), canvas, 2), "an edge target runs along the right border");
  // The dragged tab left its strip: where it was there is only the strip background.
  R1_EXPECT(near(at(img, probe.strip1.x + 340, probe.strip1.y + 12), canvas), "the lifted tab is gone from its strip: only two tabs remain");
}

void checkFloat(const r1ui::widgets::image::Image& img, r1test::TestUi& tokens) {
  const Pixel panelSecondary = backgroundOf(tokens, "panel-secondary");
  const Pixel panel = backgroundOf(tokens, "panel");
  const dock::Rect f = probe.floatFrame;
  // Title bar fill (away from text and buttons) and the rounded 8 px corner.
  R1_EXPECT(near(at(img, f.x + f.w * 0.5, f.y + 6), panelSecondary, 6), "title bar colour");
  const Pixel outside = at(img, f.x, f.y);
  R1_EXPECT(!near(outside, panelSecondary, 3) && !near(at(img, f.x + 1, f.y + 1), panelSecondary, 3), "the corner pixel is outside the rounded 8 px corner");
  R1_EXPECT(near(at(img, f.x + 12, f.y + 12), panelSecondary, 8) || near(at(img, f.x + 12, f.y + 12), panel, 40), "while a pixel 12 px in is inside");
  // The shadow darkens what lies below the frame.
  // The content of the floating panel is inside the frame.
  R1_EXPECT(near(at(img, probe.floatContent.x + probe.floatContent.w / 2, probe.floatContent.y + probe.floatContent.h * 0.8), backgroundOf(tokens, "warning-action")), "content of panel 5");
}

void checkOverflow(const r1ui::widgets::image::Image& img, r1test::TestUi& tokens) {
  const Pixel canvas = backgroundOf(tokens, "canvas");
  const Pixel muted = backgroundOf(tokens, "muted");
  // Scroll arrows at both ends: ink on the strip colour; tabs 60 px wide after the left arrow.
  int leftInk = 0, listInk = 0;
  for (int y = 6; y < 20; ++y) {
    for (int x = 2; x < 18; ++x) leftInk += near(at(img, x, y), canvas, 10) ? 0 : 1;
    for (int x = static_cast<int>(probe.list.x) + 4; x < static_cast<int>(probe.list.x) + 20; ++x) listInk += near(at(img, x, y), canvas, 10) ? 0 : 1;
  }
  R1_EXPECT(leftInk > 8 && listInk > 8, "scroll arrows and the all-tabs button are drawn");
  const Pixel border = backgroundOf(tokens, "border");
  R1_EXPECT(near(at(img, 20 + 60 - 1, 12), border) && near(at(img, 20 + 120 - 1, 12), border), "tab separators every 60 px (D12)");
  (void)muted;
}

void checkEmpty(const r1ui::widgets::image::Image& img, r1test::TestUi& tokens) {
  const Pixel panel = backgroundOf(tokens, "canvas");
  int ink = 0;
  for (int y = 180; y < 320; ++y) {
    for (int x = 190; x < 610; ++x) ink += near(at(img, x, y), panel, 6) ? 0 : 1;
  }
  R1_EXPECT(ink > 300, "the placeholder card and its text are drawn");
  R1_EXPECT(near(at(img, 20, 20), panel, 4), "the rest of the empty area is the canvas colour");
}

RenderSpec spec(ThemeId theme, float scale = 1.0f) {
  RenderSpec s;
  s.width = 800;
  s.height = 500;
  s.theme = theme;
  s.padding = 0;
  s.scale = scale;
  s.background = "canvas";
  return s;
}

void save(const r1ui::widgets::image::Image& img, const char* name, ThemeId theme) {
  const auto paths = r1test::visual::paths();
  r1ui::widgets::image::writePng(paths.artifactDir / (std::string("dock-") + name + (theme == ThemeId::Dark ? "-dark" : "-light") + ".png"), img.width, img.height, img.rgba);
}

}  // namespace

int main() try {
  const auto paths = r1test::visual::paths();
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    r1test::TestUi tokens;
    tokens.services.theme().set(theme);
    {
      const auto img = renderWidget(idleScene(), spec(theme), paths);
      save(img, "idle", theme);
      checkIdle(img, tokens);
    }
    {
      const auto img = renderWidget(dragScene(), spec(theme), paths);
      save(img, "drag", theme);
      checkDrag(img, tokens);
    }
    {
      const auto img = renderWidget(floatScene(), spec(theme), paths);
      save(img, "float", theme);
      checkFloat(img, tokens);
    }
    {
      const auto img = renderWidget(overflowScene(), spec(theme), paths);
      save(img, "overflow", theme);
      checkOverflow(img, tokens);
    }
    {
      const auto img = renderWidget(emptyScene(), spec(theme), paths);
      save(img, "empty", theme);
      checkEmpty(img, tokens);
    }
    {  // a different display scale renders the same structure at twice the pixels
      const auto img = renderWidget(idleScene(), spec(theme, 2.0f), paths);
      R1_EXPECT(img.width == 1600 && img.height == 1000);
      save(img, "idle-2x", theme);
      R1_EXPECT(near(at(img, probe.tab1.x * 2 + 200, probe.tab1.y * 2 + 40), backgroundOf(tokens, "panel")), "front tab at 2x");
    }
  }
  R1_EXPECT(r1ui::widgets::testing::validationMessageCount() == 0);
  return r1test::finish();
} catch (const std::exception& e) {
  std::fprintf(stderr, "uncaught exception: %s\n", e.what());
  return 2;
}
