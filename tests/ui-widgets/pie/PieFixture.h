// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the fixture of the pie widget tests: a headless window with a registry of eight counting
//   commands, a PieTrigger over a probe child, a provider that returns a configurable pie, recorded
//   outcomes, and helpers that drive the UiContext with synthetic right-button input on an injected clock.
// Why: every PieTrigger test needs the same dozen objects wired the same way; this keeps each test a page
//   of expectations about what the user does and what the toolkit answers.
// Callers: tests/ui-widgets/pie/*_test.cpp.
#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/commands/custommenu/CustomMenuSet.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/commands/CommandUiSync.h"
#include "r1ui/widgets/pie/PieTrigger.h"

namespace r1test {

namespace cmd = r1ui::commands;
namespace cm = r1ui::commands::custommenu;
using r1ui::core::events::Button;
using r1ui::core::events::Key;
using r1ui::core::tree::WidgetId;
using r1ui::widgets::CommandServices;
using r1ui::widgets::PieTrigger;

// A child of the trigger that records the pointer events that reach it.
class Probe final : public r1ui::widgets::WidgetObject {
 public:
  const char* typeName() const override { return "PieProbe"; }
  void onAttached() override {
    style().width = r1ui::core::layout::Length::percent(100.0);
    style().height = r1ui::core::layout::Length::percent(100.0);
  }
  void onPointerDown(Event& e) override {
    if (e.button == Button::Right) ++rightDowns;
  }
  void onClick(Event& e) override {
    if (e.button == Button::Right) ++rightClicks;
  }
  int rightDowns = 0;
  int rightClicks = 0;
};

struct PieFixture {
  explicit PieFixture(int width = 800, int height = 600)
      : t(width, height), clock(t.ui), router(registry, keymap, clock), sync(t.ui, services()) {
    for (int i = 0; i < 8; ++i) {
      const std::string id = "pie.cmd" + std::to_string(i);
      cmd::CommandDef def;
      def.id = id;
      def.label = "Command " + std::to_string(i);
      def.icon = "circle";
      def.enabled = [this, id] { return enabled[id]; };
      def.execute = [this, id](const cmd::ExecuteArgs&) {
        ++runs[id];
        if (throwing[id]) throw std::runtime_error("boom");
        return cmd::ExecuteResult::handled();
      };
      enabled[id] = true;
      R1_EXPECT(registry.add(def).ok);
    }
    pie = cm::makeEmptyMenu(cm::MenuKind::Pie, "Test pie");
    pie.id = "menu.1";
    pie.serial = 1;
    for (int i = 0; i < 8; ++i) pie.entries[static_cast<size_t>(i)] = {"pie.cmd" + std::to_string(i), "", ""};

    trigger = &t.ui.create<PieTrigger>(t.ui.root(), services(), [this](double x, double y) -> std::optional<cm::CustomMenu> {
      lastProviderPoint = {x, y};
      ++providerCalls;
      if (providerThrows) throw std::runtime_error("provider");
      return provider;
    });
    provider = pie;
    probe = &t.ui.create<Probe>(trigger->id());
    trigger->setOnExecuted([this](const std::string& id, const cmd::ExecuteResult& r) {
      executed.push_back(id);
      lastResult = r;
    });
    trigger->setOnFallback([this](double x, double y) { fallbacks.push_back({x, y}); });
    trigger->setOnCancelled([this] { ++cancelled; });
    t.ui.setTime(1000);
    t.layout();
  }

  CommandServices services() { return {registry, overrides, keymap, router}; }

  // ---- input on the injected clock ----
  void at(uint64_t ms) { t.ui.setTime(ms); }
  void pressRight(double x, double y) {
    t.ui.pointerMove(x, y);
    t.ui.pointerDown(x, y, Button::Right);
  }
  void moveTo(double x, double y) { t.ui.pointerMove(x, y); }
  void releaseRight(double x, double y) { t.ui.pointerUp(x, y, Button::Right); }
  // Time passes: due timers run and the frame places the overlay.
  void advanceTo(uint64_t ms) {
    t.ui.setTime(ms);
    t.ui.tick();
    t.layout();
  }
  // The point `distance` px from (cx, cy) in the direction of slot `slot` of an 8-slot pie.
  static std::pair<double, double> slotPoint(double cx, double cy, int slot, double distance, int count = 8) {
    const r1ui::widgets::PiePoint p = r1ui::widgets::pieSlotOffset(count, slot, distance);
    return {cx + p.x, cy + p.y};
  }
  int totalRuns() const {
    int total = 0;
    for (const auto& r : runs) total += r.second;
    return total;
  }

  r1test::TestUi t;
  cmd::CommandRegistry registry;
  cmd::KeybindingOverrides overrides{registry};
  cmd::Keymap keymap{registry, overrides};
  r1ui::widgets::UiClock clock;
  cmd::CommandRouter router;
  r1ui::widgets::CommandUiSync sync;
  cm::CustomMenu pie;
  std::optional<cm::CustomMenu> provider;
  PieTrigger* trigger = nullptr;
  Probe* probe = nullptr;
  bool providerThrows = false;
  int providerCalls = 0;
  std::pair<double, double> lastProviderPoint{0, 0};
  std::map<std::string, int> runs;
  std::map<std::string, bool> enabled;
  std::map<std::string, bool> throwing;
  std::vector<std::string> executed;
  cmd::ExecuteResult lastResult;
  std::vector<std::pair<double, double>> fallbacks;
  int cancelled = 0;
};

}  // namespace r1test
