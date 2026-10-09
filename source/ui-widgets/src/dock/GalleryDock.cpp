// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: buildGalleryDock (see GalleryDock.h): the sample panel contents (solid colour with the
//   panel's name), the box that owns the registry, backend and host, and the one-time opening of the
//   floating panel once the host has a size.
// Invariants: the box is the only owner of the registry and backend; the host is its child, so it is
//   destroyed first; the floating panel is opened after the first layout (its position is centred in
//   the host, which needs the host's rectangle).
// Callers: the gallery preview, tests.
#include "r1ui/widgets/dock/GalleryDock.h"

#include <memory>

#include "r1ui/widgets/dock/DockHost.h"
#include "r1ui/widgets/dock/InWindowFloatingBackend.h"
#include "r1ui/widgets/label/Label.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace State = theme::State;
using core::tree::WidgetId;
using theme::StyleProperty;

namespace {

constexpr theme::StyleRuleEntry kSampleRows[] = {
    {"dock.sample.1", State::kNone, StyleProperty::Background, "color:accent"},
    {"dock.sample.2", State::kNone, StyleProperty::Background, "color:component"},
    {"dock.sample.3", State::kNone, StyleProperty::Background, "color:success-bg"},
    {"dock.sample.4", State::kNone, StyleProperty::Background, "color:error"},
    {"dock.sample.5", State::kNone, StyleProperty::Background, "color:warning-action"},
    {"dock.sample.6", State::kNone, StyleProperty::Background, "color:code-tag"},
    {"dock.sample.7", State::kNone, StyleProperty::Background, "color:code-attribute"},
    {"dock.sample.text", State::kNone, StyleProperty::Foreground, "color:panel"},
    {"dock.sample.text", State::kNone, StyleProperty::FontSize, "fontSize:sm"},
    {"dock.sample.text", State::kNone, StyleProperty::FontWeight, "weight:semibold"},
    {"dock.sample.text", State::kNone, StyleProperty::LineHeight, "number:20"},
};

// A panel's content: a solid colour with the panel's name, so moves between regions are easy to see.
class SampleContent : public WidgetObject {
 public:
  SampleContent(int number, std::string name) : number_(number), name_(std::move(name)) {}
  static std::span<const theme::StyleRuleEntry> styleRows() { return kSampleRows; }
  const char* typeName() const override { return "DockSampleContent"; }
  void onAttached() override { setFocusable(true); }
  void paint(PaintContext& ctx) override {
    const std::string key = "dock.sample." + std::to_string(number_);
    ctx.painter().fillRect(ctx.box(), ctx.color(ctx.resolve(key, 0).background));
    TextOptions o;
    o.align = TextAlign::Center;
    ctx.drawText(name_, ctx.resolve("dock.sample.text", 0).text, ctx.box(), o);
    if (focusVisible()) ctx.focusRing(ctx.box(), 0.0f);
  }

 private:
  int number_;
  std::string name_;
};

// Owns everything the live dock needs and opens the floating panel when the host first has a size.
class GalleryDockBox : public WidgetObject {
 public:
  const char* typeName() const override { return "GalleryDockBox"; }

  void onAttached() override {
    layout::Style& s = style();
    s.width = layout::Length::percent(100);
    s.height = layout::Length::px(440);
    s.flexShrink = 0.0;
    s.direction = layout::FlexDirection::Column;
    setWantsLayoutCallback(true);

    const char* names[] = {"Hierarchy", "Assets", "Viewport", "Console", "Details", "Timeline", "Floating"};
    for (int i = 0; i < 7; ++i) {
      PanelDescriptor d;
      d.id = static_cast<dock::PanelId>(i + 1);
      d.title = names[i];
      d.icon = i == 2 ? "layout-dashboard" : "";
      d.floatSize = {320.0, 220.0};
      d.minSize = {120.0, 80.0};
      d.factory = [number = i + 1, name = std::string(names[i])](UiContext& ui, WidgetId parent) { return ui.create<SampleContent>(parent, number, name).id(); };
      registry_.add(std::move(d));
    }
    backend_ = std::make_unique<InWindowFloatingBackend>(ui(), ui().root());
    DockHostOptions options;
    options.floatWhenUnplaced = true;
    DockHost& host = ui().create<DockHost>(id(), registry_, *backend_, options);
    host_ = host.id();

    host.setLayout(makeLayout());
    host.setDefaultLayout([this] { return makeLayout(); });
  }

  void onLayout() override {
    if (floated_) return;
    DockHost* host = ui().objectAs<DockHost>(host_);
    if (host == nullptr || ui().absRect(host_).w <= 0.0) return;
    floated_ = true;
    host->openPanel(7);
  }

 private:
  // The default arrangement: two regions stacked on the left, tabs in the middle and on the right.
  dock::DockLayout makeLayout() const {
    using dock::Axis;
    using dock::Node;
    const Node left = Node::split(Axis::Column, {Node::stack({1}), Node::stack({2})}, 1.0);
    const Node middle = Node::stack({3, 4}, 0, 2.5);
    const Node right = Node::stack({5, 6}, 0, 1.2);
    dock::DockLayoutResult created = dock::DockLayout::create(registry_.infos(), {}, Node::split(Axis::Row, {left, middle, right}));
    return std::move(*created.layout);
  }

  PanelRegistry registry_;
  std::unique_ptr<InWindowFloatingBackend> backend_;
  WidgetId host_;
  bool floated_ = false;
};

}  // namespace

void buildGalleryDock(UiContext& ui, WidgetId parent) {
  Label& caption = ui.create<Label>(parent, "Dock host: drag a tab to another region, to an edge or out of the window; right-click a tab; drag the splitters", LabelRole::Caption);
  caption.style().margin[layout::kBottom] = layout::Length::px(8);
  ui.create<GalleryDockBox>(parent);
}

}  // namespace r1ui::widgets
