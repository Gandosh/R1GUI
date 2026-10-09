// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for the "apply variable" icon: the reference crop of the 20 px icon button
//   (widget-icon-button-small-idle) shows a nested diamond, not Lucide's plain diamond. The test draws
//   assets/icons/custom/apply-variable.svg at 14 px in `muted` on the field colour through IconCache
//   and compares the ink (sum of coverage) of the 20 x 20 button area with the reference within 15%,
//   and checks the old plain diamond would have been further away.
// Why: the Phase 3 panel used the plain diamond; this pins the replacement to the reference.
// Callers: CTest (label gpu).
#include <cmath>

#include "VisualSupport.h"
#include "r1ui/widgets/runtime/PaintContext.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;

class IconButtonProbe final : public WidgetObject {
 public:
  explicit IconButtonProbe(std::string icon) : icon_(std::move(icon)) {}
  const char* typeName() const override { return "IconButtonProbe"; }
  void onAttached() override {
    style().width = layout::Length::px(20);
    style().height = layout::Length::px(20);
  }
  void paint(PaintContext& ctx) override {
    ctx.painter().fillRect(ctx.box(), ctx.color("panel-field"));
    ctx.drawIcon(icon_, 14, ctx.box(), ctx.color("muted"));
  }

 private:
  std::string icon_;
};

image::Image renderIcon(const std::string& icon, const VisualPaths& paths) {
  RenderSpec spec;
  spec.width = 32;
  spec.height = 32;
  const BuildFn build = [&](UiContext& ui, WidgetId parent) { return ui.create<IconButtonProbe>(parent, icon).id(); };
  return renderWidget(build, spec, paths);
}

// Mean absolute red-channel difference over the 20 x 20 button area.
double regionDiff(const image::Image& a, const image::Image& b) {
  double sum = 0.0;
  for (uint32_t y = 6; y < 26; ++y) {
    for (uint32_t x = 6; x < 26; ++x) sum += std::abs(static_cast<int>(a.rgba[(y * a.width + x) * 4]) - static_cast<int>(b.rgba[(y * b.width + x) * 4]));
  }
  return sum / 400.0;
}

}  // namespace

int main() {
  const VisualPaths paths = r1test::visual::paths();
  const auto ref = image::loadPng(paths.referenceDir / "openpencil" / "dark" / "widget-icon-button-small-idle.png");
  R1_EXPECT(ref.ok());
  if (!ref.ok()) return r1test::finish();
  const auto& theme = sharedServices(paths).theme();
  const double referenceInk = inkSum(*ref.image, 6, 6, 20, 20, *theme.color("panel-field"), *theme.color("muted"));
  const image::Image nestedImage = renderIcon("apply-variable", paths);
  const image::Image plainImage = renderIcon("diamond", paths);
  const double nested = inkSum(nestedImage, 6, 6, 20, 20, *theme.color("panel-field"), *theme.color("muted"));
  const double nestedDiff = regionDiff(nestedImage, *ref.image);
  const double plainDiff = regionDiff(plainImage, *ref.image);
  std::printf("apply-variable icon: ink ratio %.3f, mean difference %.2f (plain Lucide diamond: %.2f)\n", nested / referenceInk, nestedDiff, plainDiff);
  R1_EXPECT(std::abs(nested / referenceInk - 1.0) <= 0.10);
  R1_EXPECT(nestedDiff < plainDiff);
  return r1test::finish();
}
