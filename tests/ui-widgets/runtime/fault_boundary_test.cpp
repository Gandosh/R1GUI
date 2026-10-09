// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression tests for the input fault boundary of UiContext (phase 4 review M9 and the general
//   rule "no exception escapes an input handler"): a handler or timer callback that throws is reported
//   through UiHost::reportFault, counted, releases the pointer capture, and leaves the context usable.
// Callers: CTest (fast tier). Calls: UiContext, WidgetObject.
#include <stdexcept>
#include <string>

#include "TestSupport.h"

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;
namespace events = r1ui::core::events;

namespace {

enum class Mode { None, Std, Other };

class Thrower : public WidgetObject {
 public:
  const char* typeName() const override { return "Thrower"; }
  void onAttached() override {
    style().width = layout::Length::px(100);
    style().height = layout::Length::px(30);
    setFocusable(true);
  }
  void maybeThrow(Mode m) {
    if (m == Mode::Std) throw std::runtime_error("handler failed");
    if (m == Mode::Other) throw 42;
  }
  void onPointerDown(Event& e) override {
    ui().router().capturePointer(id());
    e.markHandled();
    ++downs;
    maybeThrow(onDown);
  }
  void onPointerMove(Event&) override { maybeThrow(onMove); }
  void onPointerWheel(Event&) override { maybeThrow(onWheel); }
  void onKeyDown(Event&) override { maybeThrow(onKey); }
  void onTextInput(Event&) override { maybeThrow(onText); }
  Mode onDown = Mode::None, onMove = Mode::None, onWheel = Mode::None, onKey = Mode::None, onText = Mode::None;
  int downs = 0;
};

}  // namespace

int main() {
  r1test::TestUi t;
  std::string reported;
  int reports = 0;
  UiContextOptions options;
  options.host.reportFault = [&](std::string_view message) {
    reported.assign(message);
    ++reports;
  };
  UiContext ui(t.services, options);  // a second context on the same Services, with a fault hook
  ui.setViewport(400, 300, 1.0f);
  ui.setAnimationsEnabled(false);
  Thrower& w = ui.create<Thrower>(ui.root());
  ui.frame();
  ui.focusWidget(w.id());

  // A pointer handler throws: not propagated, reported, counted, capture released.
  w.onDown = Mode::Std;
  bool escaped = false;
  try {
    ui.pointerMove(10, 10);
    ui.pointerDown(10, 10);
  } catch (...) {
    escaped = true;
  }
  R1_EXPECT(!escaped);
  R1_EXPECT(ui.inputFaults() == 1 && reports == 1);
  R1_EXPECT(reported == "handler failed" && ui.lastInputFault() == "handler failed");
  R1_EXPECT(!ui.router().capturer().valid());
  w.onDown = Mode::None;
  ui.pointerUp(10, 10);

  // Every other input entry point is guarded, including a non-standard exception.
  w.onMove = Mode::Std;
  w.onWheel = Mode::Other;
  w.onKey = Mode::Std;
  w.onText = Mode::Std;
  try {
    ui.pointerMove(12, 12);
    ui.wheel(12, 12, 0, 1);
    ui.keyDown(events::Key::A);
    ui.textInput(U'a');
  } catch (...) {
    escaped = true;
  }
  R1_EXPECT(!escaped);
  R1_EXPECT(ui.inputFaults() == 5);
  R1_EXPECT(ui.lastInputFault() == "handler failed");  // the text input was the last one

  // A timer callback that throws does not stop the timers behind it.
  int later = 0;
  ui.setTime(1000);
  ui.setTimer(0, [] { throw std::runtime_error("timer failed"); });
  ui.setTimer(0, [&] { ++later; });
  try {
    ui.setTime(1010);
    ui.tick();
  } catch (...) {
    escaped = true;
  }
  R1_EXPECT(!escaped);
  R1_EXPECT(later == 1);
  R1_EXPECT(ui.inputFaults() == 6 && ui.lastInputFault() == "timer failed");

  // The context still works.
  w.onMove = w.onWheel = w.onKey = w.onText = Mode::None;
  ui.pointerDown(10, 10);
  R1_EXPECT(w.downs == 2);
  ui.pointerUp(10, 10);

  // Without a host hook the default context counts too (and reports to stderr).
  r1test::TestUi plain;
  Thrower& p = plain.ui.create<Thrower>(plain.ui.root());
  plain.ui.frame();
  p.onKey = Mode::Std;
  plain.ui.focusWidget(p.id());
  plain.ui.keyDown(events::Key::A);
  R1_EXPECT(plain.ui.inputFaults() == 1);
  return r1test::finish();
}
