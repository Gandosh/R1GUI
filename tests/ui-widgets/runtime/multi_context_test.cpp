// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the multi-window readiness test (phase 4 review fix item 4): two UiContexts that share one
//   Services (one per future OS window) render independently. It checks that each window uploads the
//   glyph atlas on its own schedule (a window that finishes first does not consume the others'
//   dirty region), that overlapping frames of two windows are detected and repainted, that frames one
//   after the other are not, that a theme switch made through one context repaints the other, that
//   style rows registered by a new widget type while the other context is mid-frame cannot invalidate
//   a style a widget holds (Services::resolve returns values), that icons reset during one window's
//   frame make that window repaint, and that destroyed widgets release their animation slots.
// Callers: CTest (fast tier). Calls: Services, UiContext, TextEngine, IconCache.
#include <memory>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/label/Label.h"

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;
namespace theme = r1ui::theme;

namespace {

// A texture that counts how often (and how much) it is updated, over the in-memory textures.
class CountingTexture final : public AtlasTexture {
 public:
  CountingTexture(std::unique_ptr<AtlasTexture> inner, int* updates) : inner_(std::move(inner)), updates_(updates) {}
  r1ui::render::TextureRef ref() const override { return inner_->ref(); }
  uint32_t width() const override { return inner_->width(); }
  uint32_t height() const override { return inner_->height(); }
  void update(uint32_t x, uint32_t y, uint32_t w, uint32_t h, std::span<const uint8_t> pixels) override {
    ++*updates_;
    inner_->update(x, y, w, h, pixels);
  }

 private:
  std::unique_ptr<AtlasTexture> inner_;
  int* updates_;
};

// Textures are numbered in creation order: Services creates the text atlas first, the icon atlas second.
class CountingFactory final : public TextureFactory {
 public:
  std::unique_ptr<AtlasTexture> createCoverage(uint32_t width, uint32_t height) override {
    updates.push_back(std::make_unique<int>(0));
    return std::make_unique<CountingTexture>(inner.createCoverage(width, height), updates.back().get());
  }
  int textUpdates() const { return *updates.at(0); }
  NullTextureFactory inner;
  std::vector<std::unique_ptr<int>> updates;
};

// A widget type with style rows of its own, registered the first time one is created.
class RowsOfItsOwn : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows() {
    static const theme::StyleRuleEntry rows[] = {{"multictx.item", theme::State::kNone, theme::StyleProperty::Background, "color:panel"}};
    return rows;
  }
  const char* typeName() const override { return "RowsOfItsOwn"; }
};

struct Window {
  Window(Services& services, const char* title) : ui(services) {
    ui.setViewport(300, 200, 1.0f);
    ui.setAnimationsEnabled(false);
    label = &ui.create<Label>(ui.root(), title, LabelRole::Body);
    ui.frame();
  }
  void begin() {
    ui.frame();
    painter.begin(300, 200);
    ui.paint(painter);
  }
  void end() {
    ui.finishPaint();
    painter.end();
  }
  void frame() {
    begin();
    end();
  }
  UiContext ui;
  Label* label = nullptr;
  r1ui::render::Painter painter;
};

}  // namespace

int main() {
  CountingFactory textures;
  Services services(r1test::loadTokens(), textures, r1test::assetPaths());
  Window a(services, "Window A: some text to rasterise");
  Window b(services, "");

  // ---- the atlas: each window uploads on its own ----
  a.frame();
  const int afterA = textures.textUpdates();
  R1_EXPECT(afterA > 0);
  b.frame();  // draws no text, but A's new glyphs are still missing from B's share
  R1_EXPECT(textures.textUpdates() > afterA);
  const int afterB = textures.textUpdates();
  b.frame();
  a.frame();
  R1_EXPECT(textures.textUpdates() == afterB);  // nothing changed since: no uploads
  R1_EXPECT(!a.ui.consumeRepaint() && !b.ui.consumeRepaint());

  // ---- frames of the two windows one after the other need no repaint; overlapping ones do ----
  a.begin();
  a.end();
  b.begin();
  b.end();
  R1_EXPECT(!a.ui.consumeRepaint() && !b.ui.consumeRepaint());
  a.begin();
  b.begin();  // A is still open
  b.end();
  a.end();
  R1_EXPECT(a.ui.consumeRepaint() && b.ui.consumeRepaint());
  a.frame();
  R1_EXPECT(!a.ui.consumeRepaint());

  // ---- a theme switch seen by both windows ----
  R1_EXPECT(!a.ui.needsFrame() && !b.ui.needsFrame());
  services.theme().toggle();
  R1_EXPECT(a.ui.needsFrame() && b.ui.needsFrame());
  a.frame();
  R1_EXPECT(!a.ui.needsFrame() && b.ui.needsFrame());  // B repaints on its own schedule
  b.frame();
  R1_EXPECT(!b.ui.needsFrame());

  // ---- style rows registered while another window is between begin and end ----
  const theme::ResolvedStyle held = services.resolve("button.ghost", 0);
  b.begin();
  const theme::ResolvedStyle heldMidFrame = services.resolve("button.ghost", 0);
  a.ui.create<RowsOfItsOwn>(a.ui.root());  // new rows: the sheet is rebuilt and the cache reset
  services.theme().toggle();                // and the theme changes under B's frame
  const theme::ResolvedStyle again = services.resolve("button.ghost", 0);
  R1_EXPECT(again.radius == held.radius && again.paddingX == held.paddingX);  // the copies are plain data that stay valid
  R1_EXPECT(heldMidFrame.radius == held.radius && heldMidFrame.paddingX == held.paddingX);
  R1_EXPECT(services.hasStyleKey("multictx.item"));
  b.end();
  a.frame();
  b.frame();
  R1_EXPECT(!a.ui.needsFrame() && !b.ui.needsFrame());

  // ---- an icon atlas reset during one window's frame makes that window repaint ----
  a.begin();
  const uint64_t epoch = services.icons().resetEpoch();
  services.icons().setAntiAlias(services.icons().antiAlias() == AntiAlias::Smooth ? AntiAlias::Msaa4 : AntiAlias::Smooth);
  R1_EXPECT(services.icons().resetEpoch() != epoch);
  a.end();
  R1_EXPECT(a.ui.consumeRepaint());
  b.frame();
  R1_EXPECT(!b.ui.consumeRepaint());  // B was not painting during the reset

  // ---- animation slots are released with the widget ----
  Window c(services, "C");
  c.ui.setAnimationsEnabled(true);
  c.ui.setFrameLoopRunning(true);
  const auto tween = c.ui.animatedValue(c.label->id(), 7, 1.0f);
  (void)tween;
  R1_EXPECT(c.ui.animationCount() == 1);
  c.ui.destroy(c.label->id());
  R1_EXPECT(c.ui.animationCount() == 0);
  return r1test::finish();
}
