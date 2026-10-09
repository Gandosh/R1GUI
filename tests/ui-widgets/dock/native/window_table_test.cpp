// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of WindowTable<W> with a fake window layer: id allocation, stacking, and above all the
//   destruction ordering the native backend relies on (retire now, destroy at the sweep, parked
//   windows in retirement order, live windows topmost first, a destructor that retires another
//   window, a window "destroyed" from inside its own handler).
// Why: the dock destroys a floating window from inside that window's own event handler; the table's
//   rules are what keep that from being a use-after-free, and they need no OS to be tested.
// Callers: CTest (label fast).
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ExpectWithMessage.h"
#include "r1ui/widgets/dock/native/WindowTable.h"

using r1ui::widgets::FloatId;
using r1ui::widgets::native::WindowTable;

namespace {

std::vector<std::string>& log() {
  static std::vector<std::string> entries;
  return entries;
}

// A window that records its destruction and may run a hook from its destructor.
struct FakeWindow {
  explicit FakeWindow(std::string n) : name(std::move(n)) {}
  ~FakeWindow() {
    log().push_back("destroy " + name);
    if (onDestroy) onDestroy();
  }
  std::string name;
  std::function<void()> onDestroy;
};

void ids_and_stacking() {
  WindowTable<FakeWindow> table;
  const FloatId a = table.add(std::make_unique<FakeWindow>("a"));
  const FloatId b = table.add(std::make_unique<FakeWindow>("b"));
  const FloatId c = table.add(std::make_unique<FakeWindow>("c"));
  R1_EXPECT(a == 1 && b == 2 && c == 3, "the main window is 0, floating windows start at 1");
  R1_EXPECT(table.order() == std::vector<FloatId>({a, b, c}), "new windows are on top");
  bool changed = false;
  R1_EXPECT(table.raise(a, &changed) && changed && table.order() == std::vector<FloatId>({b, c, a}));
  R1_EXPECT(table.raise(a, &changed) && !changed, "raising the top window changes nothing");
  R1_EXPECT(!table.raise(99), "unknown id");
  R1_EXPECT(table.find(b) != nullptr && table.find(b)->name == "b" && table.find(0) == nullptr && table.find(99) == nullptr);
  const FloatId d = table.add(std::make_unique<FakeWindow>("d"));
  R1_EXPECT(d == 4, "ids are not reused");
  table.retire(d);
  R1_EXPECT(table.add(std::make_unique<FakeWindow>("e")) == 5, "not even after a retirement");
  log().clear();
}

void retirement_is_deferred() {
  log().clear();
  WindowTable<FakeWindow> table;
  const FloatId a = table.add(std::make_unique<FakeWindow>("a"));
  const FloatId b = table.add(std::make_unique<FakeWindow>("b"));
  R1_EXPECT(table.retire(a), "retired");
  R1_EXPECT(!table.retire(a), "retiring twice is harmless and reports false");
  R1_EXPECT(table.find(a) == nullptr && table.size() == 1 && table.parkedCount() == 1, "gone from every lookup at once");
  R1_EXPECT(table.order() == std::vector<FloatId>({b}));
  R1_EXPECT(log().empty(), "but the object is still alive: the handler that retired it is still running");
  table.sweep();
  R1_EXPECT(log() == std::vector<std::string>({"destroy a"}) && table.parkedCount() == 0, "destroyed at the safe point");
  table.sweep();
  R1_EXPECT(log().size() == 1, "an empty sweep does nothing");
}

void destruction_order() {
  log().clear();
  {
    WindowTable<FakeWindow> table;
    const FloatId a = table.add(std::make_unique<FakeWindow>("a"));
    const FloatId b = table.add(std::make_unique<FakeWindow>("b"));
    const FloatId c = table.add(std::make_unique<FakeWindow>("c"));
    const FloatId d = table.add(std::make_unique<FakeWindow>("d"));
    table.retire(c);
    table.retire(a);
    table.raise(b);  // order: d, b
    R1_EXPECT(table.order() == std::vector<FloatId>({d, b}));
  }
  R1_EXPECT(log() == std::vector<std::string>({"destroy c", "destroy a", "destroy b", "destroy d"}),
            "parked windows first in retirement order, then the live ones from the topmost down");
}

void destructor_retires_another() {
  log().clear();
  WindowTable<FakeWindow> table;
  const FloatId a = table.add(std::make_unique<FakeWindow>("a"));
  const FloatId b = table.add(std::make_unique<FakeWindow>("b"));
  table.find(a)->onDestroy = [&] { table.retire(b); };  // closing a window closes its sibling
  table.retire(a);
  table.sweep();
  R1_EXPECT(log() == std::vector<std::string>({"destroy a"}) && table.parkedCount() == 1, "the sibling waits for the next sweep");
  table.sweep();
  R1_EXPECT(log() == std::vector<std::string>({"destroy a", "destroy b"}) && table.size() == 0);
}

void destroyed_from_its_own_handler() {
  log().clear();
  WindowTable<FakeWindow> table;
  const FloatId a = table.add(std::make_unique<FakeWindow>("a"));
  bool handlerFinished = false;
  // The "handler" of window a retires a, keeps using its pointer, and only then does the loop sweep.
  FakeWindow* self = table.find(a);
  self->onDestroy = [&] { R1_EXPECT(handlerFinished, "the object outlived the handler that retired it"); };
  table.retire(a);
  R1_EXPECT(self->name == "a", "still valid after retire");
  handlerFinished = true;
  table.sweep();
  R1_EXPECT(log() == std::vector<std::string>({"destroy a"}));
}

}  // namespace

int main() {
  ids_and_stacking();
  retirement_is_deferred();
  destruction_order();
  destructor_retires_another();
  destroyed_from_its_own_handler();
  return r1test::finish();
}
