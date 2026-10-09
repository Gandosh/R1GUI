// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression test for the teardown of a UiContext that still holds widgets. A native floating window
//   owns a context that is destroyed together with the window while the application's models, registries and
//   notifiers live on; widgets that subscribe to such objects in onAttached unsubscribe in onDetached, but
//   the context's destructor freed them without that call, so the subscription kept a dangling callback and
//   the next change crashed (found by the preview's Editor screen: a property panel, the command palette
//   and the shortcut editor in a window that was then closed). The destructor now detaches every widget
//   first, children before parents, and a hook that throws does not stop the teardown.
// Callers: CTest (runtime fast, no GPU).
#include <memory>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/widgets/section/Section.h"

namespace {

using namespace r1ui::widgets;

// A widget that registers with an outside object (a counter of live subscriptions) and leaves in onDetached.
class Subscriber final : public WidgetObject {
 public:
  Subscriber(int* live, std::vector<std::string>* order, std::string name, bool throws = false) : live_(live), order_(order), name_(std::move(name)), throws_(throws) {}
  const char* typeName() const override { return "Subscriber"; }
  void onAttached() override { ++*live_; }
  void onDetached() override {
    --*live_;
    order_->push_back(name_);
    if (throws_) throw std::runtime_error("a hook that throws");
  }

 private:
  int* live_;
  std::vector<std::string>* order_;
  std::string name_;
  bool throws_;
};

}  // namespace

int main() {
  int live = 0;
  std::vector<std::string> order;
  {
    r1test::TestUi t(200, 200);
    SectionBox& box = t.ui.create<SectionBox>(t.ui.root());
    t.ui.create<Subscriber>(box.id(), &live, &order, "child");
    t.ui.create<Subscriber>(t.ui.root(), &live, &order, "thrower", true);
    t.ui.create<Subscriber>(box.id(), &live, &order, "second child");
    R1_EXPECT(live == 3);  // three subscriptions exist while the context lives
    t.layout();
  }
  R1_EXPECT(live == 0);  // destroying the context detached every widget (no subscription left behind)
  // The latest created goes first, children before their parent: a widget made after another may depend on it.
  R1_EXPECT((order == std::vector<std::string>{"thrower", "second child", "child"}));
  return r1test::finish();
}
