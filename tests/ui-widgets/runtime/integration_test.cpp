// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression tests of the integration bugs found when widgets of different groups were first
//   composed in one UiContext (slice 4.17): the guarded focus API (a blur handler that destroys its
//   own widget must not free it while the router still runs it) and the rule that a focused text
//   widget keeps unmodified letters away from the application's global shortcuts.
// Callers: CTest (label fast).
#include <memory>

#include "TestSupport.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace {

using namespace r1ui::widgets;
namespace events = r1ui::core::events;
constexpr events::Key kT = static_cast<events::Key>('T');

// A focusable widget that destroys itself when it loses focus. `dead` is set by its destructor, so
// the blur handler can tell whether the object was freed while the handler was still running.
class SelfDestructing final : public WidgetObject {
 public:
  SelfDestructing(std::shared_ptr<bool> dead, std::shared_ptr<bool> freedInsideHandler) : dead_(std::move(dead)), freed_(std::move(freedInsideHandler)) {}
  ~SelfDestructing() override { *dead_ = true; }
  const char* typeName() const override { return "SelfDestructing"; }
  void onAttached() override {
    setFocusable(true);
    style().width = r1ui::core::layout::Length::px(40);
    style().height = r1ui::core::layout::Length::px(20);
  }
  void onFocusOut(Event&) override {
    ui().destroy(id());
    *freed_ = *dead_;  // true when destroy() freed this very object under the running handler
  }

 private:
  std::shared_ptr<bool> dead_;
  std::shared_ptr<bool> freed_;
};

class Plain final : public WidgetObject {
 public:
  const char* typeName() const override { return "Plain"; }
  void onAttached() override {
    setFocusable(true);
    style().width = r1ui::core::layout::Length::px(40);
    style().height = r1ui::core::layout::Length::px(20);
  }
};

struct Recorder final : events::GlobalKeyHandler {
  bool onGlobalKey(const events::Event& e, events::Router&) override {
    keys.push_back(e.key);
    return true;
  }
  std::vector<events::Key> keys;
};

void testFocusFromOutsideADispatchFrame() {
  r1test::TestUi t;
  auto dead = std::make_shared<bool>(false);
  auto freed = std::make_shared<bool>(true);
  const WidgetObject& first = t.ui.create<SelfDestructing>(t.ui.root(), dead, freed);
  const auto firstId = first.id();
  Plain& second = t.ui.create<Plain>(t.ui.root());
  t.layout();
  R1_EXPECT(t.ui.focusWidget(firstId));
  R1_EXPECT(t.ui.router().focused() == firstId);
  // Moving focus away runs the blur handler of the first widget, which destroys it.
  R1_EXPECT(t.ui.focusWidget(second.id()));
  R1_EXPECT(!*freed);  // the object outlived the handler that destroyed it
  R1_EXPECT(*dead);    // and is gone once the call returned
  R1_EXPECT(!t.ui.alive(firstId));
  R1_EXPECT(t.ui.router().focused() == second.id());
  t.ui.clearFocus();
  R1_EXPECT(!t.ui.router().focused().valid());
  R1_EXPECT(!t.ui.focusWidget(firstId));  // a stale id is refused
}

void testTypedLettersAreNotShortcuts() {
  r1test::TestUi t;
  Recorder keys;
  t.ui.setGlobalKeyHandler(&keys);
  TextInput& input = t.ui.create<TextInput>(t.ui.root());
  Plain& plain = t.ui.create<Plain>(t.ui.root());
  t.layout();
  R1_EXPECT(t.ui.focusWidget(input.id()));
  t.ui.keyDown(kT);
  t.ui.keyDown(events::Key::Space);
  R1_EXPECT(keys.keys.empty());  // the field owns unmodified printable keys
  t.ui.keyDown(events::Key::F1);
  R1_EXPECT(keys.keys.size() == 1);  // other keys still reach the shortcuts
  t.ui.keyDown(kT, events::Mod::kCtrl);
  R1_EXPECT(keys.keys.size() == 2);  // a chord is never text
  R1_EXPECT(t.ui.focusWidget(plain.id()));
  t.ui.keyDown(kT);
  R1_EXPECT(keys.keys.size() == 3);  // a widget that takes no text lets the letter through
}

}  // namespace

int main() {
  testFocusFromOutsideADispatchFrame();
  testTypedLettersAreNotShortcuts();
  return r1test::finish();
}
