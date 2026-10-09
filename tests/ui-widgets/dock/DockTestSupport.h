// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the rig the dock widget tests share: a headless TestUi, a PanelRegistry whose panels create
//   a small focusable content widget (counting how often their factory ran), the in-window floating
//   backend and a DockHost filling the window, plus helpers to find tabs, bodies and handles and to
//   drive synthetic pointer drags.
// Why: every dock test needs the same scaffolding; keeping it here leaves the tests about behaviour.
// Callers: tests/ui-widgets/dock/*_test.cpp.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/widgets/dock/DockHost.h"
#include "r1ui/widgets/dock/InWindowFloatingBackend.h"

// The dock tests state what each expectation is about: R1_EXPECT(condition, "what").
#undef R1_EXPECT
#define R1_EXPECT(cond, ...) ::r1test::report(static_cast<bool>(cond), #cond " " #__VA_ARGS__, __FILE__, __LINE__)

namespace dock_widget_test {

using namespace r1ui::widgets;
namespace dock = r1ui::dock;
using r1ui::core::events::Button;
using r1ui::core::events::Key;
using r1ui::core::tree::WidgetId;

// A panel's content: focusable so keyboard focus can move into it.
class TestContent : public WidgetObject {
 public:
  explicit TestContent(int id) : number(id) {}
  const char* typeName() const override { return "TestContent"; }
  void onAttached() override { setFocusable(true); }
  int number;
};

struct DockRig {
  DockRig() : DockRig(900, 600, 6, DockHostOptions{}) {}

  DockRig(int width, int height, dock::PanelId count, DockHostOptions options, bool createHost = true)
      : t(width, height) {
    for (dock::PanelId id = 1; id <= count; ++id) {
      PanelDescriptor d;
      d.id = id;
      d.title = "Panel " + std::to_string(id);
      d.floatSize = {300.0, 200.0};
      d.factory = [this, id](UiContext& ui, WidgetId parent) {
        ++factoryCalls[id];
        return ui.create<TestContent>(parent, static_cast<int>(id)).id();
      };
      registry.add(std::move(d));
    }
    backend = std::make_unique<InWindowFloatingBackend>(t.ui, t.ui.root());
    if (createHost) {
      host = &t.ui.create<DockHost>(t.ui.root(), registry, *backend, options);
      host->setOnChanged([this](DockChange c) { changes.push_back(c); });
    }
  }

  // Installs a main-area tree and lays everything out.
  void setRoot(dock::Node root) {
    dock::DockLayoutResult created = dock::DockLayout::create(registry.infos(), host->options().config, std::move(root));
    if (!created.ok()) {
      std::fprintf(stderr, "rig: %s\n", created.error.c_str());
      std::abort();
    }
    host->setLayout(std::move(*created.layout));
    settle();
  }

  void settle() {
    for (int i = 0; i < 4; ++i) t.ui.frame();
  }

  // ---- geometry helpers (logical px, window coordinates) ----
  DockTabStrip* strip(dock::PanelId panel) const { return host->stripOf(panel); }
  dock::Rect tab(dock::PanelId panel) const {
    DockTabStrip* s = strip(panel);
    return s == nullptr ? dock::Rect{} : s->tabRect(*s->indexOf(panel));
  }
  dock::Point tabCenter(dock::PanelId panel) const {
    const dock::Rect r = tab(panel);
    return {r.x + r.w / 2.0, r.y + r.h / 2.0};
  }
  dock::Rect closeButton(dock::PanelId panel) const {
    DockTabStrip* s = strip(panel);
    return s == nullptr ? dock::Rect{} : s->closeRect(*s->indexOf(panel));
  }
  // The body rectangle of the stack showing `panel` as its front tab.
  dock::Rect body(dock::PanelId panel) const {
    for (const dock::StackLayout& s : host->layout().computeLayout(host->mainContentRect()).stacks) {
      for (const dock::TabLayout& tabInfo : s.tabs) {
        if (tabInfo.panel == panel) return s.body;
      }
    }
    return {};
  }

  // ---- input helpers ----
  void move(dock::Point p) { t.ui.pointerMove(p.x, p.y); }
  void down(dock::Point p, Button b = Button::Left) { t.ui.pointerDown(p.x, p.y, b); }
  void up(dock::Point p, Button b = Button::Left) { t.ui.pointerUp(p.x, p.y, b); }
  void click(dock::Point p, Button b = Button::Left) {
    move(p);
    down(p, b);
    up(p, b);
    settle();
  }
  // Presses at `from`, moves in steps to `to` (more than the drag threshold) and releases there.
  void drag(dock::Point from, dock::Point to, bool release = true) {
    move(from);
    down(from);
    const int steps = 8;
    for (int i = 1; i <= steps; ++i) {
      move({from.x + (to.x - from.x) * i / steps, from.y + (to.y - from.y) * i / steps});
      t.ui.frame();
    }
    if (release) {
      up(to);
      settle();
    }
  }
  const std::vector<dock::StackLayout>& stacks() {
    result = host->layout().computeLayout(host->mainContentRect());
    return result.stacks;
  }

  r1test::TestUi t;
  PanelRegistry registry;
  std::unique_ptr<InWindowFloatingBackend> backend;
  DockHost* host = nullptr;
  std::map<dock::PanelId, int> factoryCalls;
  std::vector<DockChange> changes;
  dock::LayoutResult result;
};

inline dock::Node stackOf(std::vector<dock::PanelId> tabs, size_t active = 0, double weight = 1.0) {
  return dock::Node::stack(std::move(tabs), active, weight);
}

}  // namespace dock_widget_test
