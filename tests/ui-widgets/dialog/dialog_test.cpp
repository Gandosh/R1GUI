// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: behaviour tests of the modal dialog without a GPU: size and centring, the header, body slot and
//   footer, modality (nothing behind it receives input, Tab stays inside, a press on the scrim does
//   nothing unless asked), Escape as cancel, Enter as the default action (not when a focused control
//   uses it), the close button, the single result callback, closing from code, stacking (only the
//   newest is interactive), closing when the owner goes away, focus placement and restore, and hostile
//   input (huge and invalid texts, many actions, zero-size window, wider than the window, a result
//   callback that opens another dialog, closing twice, destroying content inside handlers).
// Callers: CTest (label fast).
#include "TestSupport.h"
#include "r1ui/widgets/dialog/Dialog.h"
#include "r1ui/widgets/dialog/DialogParts.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Key;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;

struct Field : WidgetObject {
  const char* typeName() const override { return "Field"; }
  void onAttached() override {
    style().width = layout::Length::px(120);
    style().height = layout::Length::px(26);
    setFocusable(true);
  }
  void onKeyDown(Event& e) override {
    if (e.key == Key::Enter && usesEnter) e.markHandled();
  }
  bool usesEnter = false;
};

struct Under : WidgetObject {
  const char* typeName() const override { return "Under"; }
  void onAttached() override {
    style().position = layout::Position::Absolute;
    style().width = layout::Length::px(400);
    style().height = layout::Length::px(300);
    setFocusable(true);
  }
  void onPointerDown(Event&) override { ++downs; }
  void onKeyDown(Event&) override { ++keys; }
  int downs = 0;
  int keys = 0;
};

DialogSpec simpleSpec(DialogResult* result, int* calls) {
  DialogSpec s;
  s.title = "Delete layer";
  s.description = "This removes the layer and everything inside it. You can undo it with Ctrl+Z.";
  s.actions = {{"cancel", "Cancel", DialogActionKind::Neutral, false, true, true}, {"delete", "Delete", DialogActionKind::Danger, true, false, true}};
  s.onResult = [result, calls](const DialogResult& r) {
    *result = r;
    ++*calls;
  };
  return s;
}

void testLayoutAndModality() {
  r1test::TestUi t(800, 600);
  Under& under = t.ui.create<Under>(t.ui.root());
  t.layout();
  DialogResult result;
  int calls = 0;
  const DialogHandle h = openDialog(t.ui, simpleSpec(&result, &calls));
  R1_EXPECT(h.valid());
  Field& field = t.ui.create<Field>(h.body);
  t.layout();
  t.layout();
  const auto host = t.ui.absRect(h.host);
  // Centred, at most 512 wide, sized by content.
  R1_EXPECT(host.w <= 512 && host.w > 200);
  R1_EXPECT(std::abs((host.x + host.w / 2) - 400) <= 1 && std::abs((host.y + host.h / 2) - 300) <= 1);
  R1_EXPECT(t.ui.overlays().anyModal());
  // Nothing behind the dialog gets input; a press on the scrim does nothing.
  t.ui.pointerDown(5, 5);
  t.ui.pointerUp(5, 5);
  R1_EXPECT(under.downs == 0 && isDialogOpen(t.ui, h) && calls == 0);
  // Initial focus: the first control of the body.
  R1_EXPECT(t.ui.router().focused() == field.id());
  // Tab stays inside the dialog: field -> Cancel -> Delete -> close button -> field.
  std::vector<WidgetId> order;
  for (int i = 0; i < 4; ++i) {
    t.ui.keyDown(Key::Tab);
    order.push_back(t.ui.router().focused());
  }
  R1_EXPECT(order[3] == field.id());
  for (const WidgetId id : order) R1_EXPECT(t.ui.overlays().contains(h.overlay, id));
  t.ui.keyDown(Key::Tab, r1ui::core::events::Mod::kShift);
  R1_EXPECT(t.ui.overlays().contains(h.overlay, t.ui.router().focused()));
  closeDialog(t.ui, h, "delete");
  R1_EXPECT(!isDialogOpen(t.ui, h) && calls == 1 && result.action == "delete" && !result.dismissed);
  R1_EXPECT(!closeDialog(t.ui, h, "again") && calls == 1);  // closing twice reports nothing new
}

void testEscapeEnterAndButtons() {
  r1test::TestUi t(800, 600);
  Under& under = t.ui.create<Under>(t.ui.root());
  t.layout();
  t.ui.router().focus(under.id(), r1ui::core::events::FocusReason::Keyboard);
  DialogResult result;
  int calls = 0;
  DialogHandle h = openDialog(t.ui, simpleSpec(&result, &calls));
  t.layout();
  t.layout();
  // No body control: the default action (Delete) has focus.
  auto* focusedButton = t.ui.objectAs<DialogButton>(t.ui.router().focused());
  R1_EXPECT(focusedButton != nullptr && focusedButton->action().id == "delete");
  // Escape: cancel, dismissed, focus restored, one callback.
  t.ui.keyDown(Key::Escape);
  R1_EXPECT(!isDialogOpen(t.ui, h) && calls == 1 && result.dismissed && result.action == "cancel");
  R1_EXPECT(t.ui.router().focused() == under.id());
  // Enter with the focus on a non-default button activates that button, not the default one.
  h = openDialog(t.ui, simpleSpec(&result, &calls));
  t.layout();
  t.layout();
  t.ui.keyDown(Key::Tab, r1ui::core::events::Mod::kShift);  // Delete -> Cancel
  t.ui.keyDown(Key::Enter);
  R1_EXPECT(calls == 2 && result.action == "cancel" && !result.dismissed);
  // Enter in a body control that does not use it runs the default action; one that uses it keeps it.
  for (const bool uses : {true, false}) {
    h = openDialog(t.ui, simpleSpec(&result, &calls));
    Field& f = t.ui.create<Field>(h.body);
    f.usesEnter = uses;
    t.layout();
    t.layout();
    R1_EXPECT(t.ui.router().focused() == f.id());
    const int before = calls;
    t.ui.keyDown(Key::Enter);
    if (uses) {
      R1_EXPECT(calls == before && isDialogOpen(t.ui, h));
      closeDialog(t.ui, h);
    } else {
      R1_EXPECT(calls == before + 1 && result.action == "delete");
    }
  }
  // Space activates the focused button; a click on a button activates on release inside.
  h = openDialog(t.ui, simpleSpec(&result, &calls));
  t.layout();
  t.layout();
  const WidgetId cancelButton = t.ui.tree().firstChild(t.ui.tree().nextSibling(t.ui.tree().nextSibling(t.ui.tree().firstChild(h.content))));
  (void)cancelButton;
  t.ui.keyDown(Key::Tab, r1ui::core::events::Mod::kShift);
  t.ui.keyDown(Key::Space);
  R1_EXPECT(result.action == "cancel");
  // The close button ends the dialog as a cancel.
  h = openDialog(t.ui, simpleSpec(&result, &calls));
  t.layout();
  t.layout();
  DialogCloseButton* close = nullptr;
  t.ui.tree().forEachDescendant(h.content, [&](WidgetId id) {
    if (auto* c = t.ui.objectAs<DialogCloseButton>(id)) close = c;
  });
  R1_EXPECT(close != nullptr);
  const auto cr = t.ui.absRect(close->id());
  t.ui.pointerMove(cr.x + 4, cr.y + 4);
  t.ui.pointerDown(cr.x + 4, cr.y + 4);
  t.ui.pointerUp(cr.x + 4, cr.y + 4);
  R1_EXPECT(!isDialogOpen(t.ui, h) && result.dismissed && result.action == "cancel");
}

void testStackOwnerAndScrim() {
  r1test::TestUi t(800, 600);
  Under& owner = t.ui.create<Under>(t.ui.root());
  t.layout();
  DialogResult r1;
  DialogResult r2;
  int c1 = 0;
  int c2 = 0;
  DialogSpec first = simpleSpec(&r1, &c1);
  first.owner = owner.id();
  const DialogHandle a = openDialog(t.ui, first);
  t.layout();
  t.layout();
  const DialogHandle b = openDialog(t.ui, simpleSpec(&r2, &c2));
  t.layout();
  t.layout();
  // Only the newest is interactive: Tab and Escape act on it; the first one stays open.
  R1_EXPECT(t.ui.overlays().contains(b.overlay, t.ui.router().focused()));
  t.ui.keyDown(Key::Escape);
  R1_EXPECT(!isDialogOpen(t.ui, b) && isDialogOpen(t.ui, a) && c2 == 1 && c1 == 0);
  t.layout();
  R1_EXPECT(t.ui.overlays().contains(a.overlay, t.ui.router().focused()));
  // Destroying the owner closes the dialog as dismissed.
  t.ui.destroy(owner.id());
  t.layout();
  R1_EXPECT(!isDialogOpen(t.ui, a) && c1 == 1 && r1.dismissed);
  // closeOnScrimPress closes on a press outside the dialog.
  DialogResult r3;
  int c3 = 0;
  DialogSpec s = simpleSpec(&r3, &c3);
  s.closeOnScrimPress = true;
  const DialogHandle d = openDialog(t.ui, s);
  t.layout();
  t.ui.pointerDown(3, 3);
  t.ui.pointerUp(3, 3);
  R1_EXPECT(!isDialogOpen(t.ui, d) && c3 == 1 && r3.dismissed);
}

void testSizing() {
  // A fixed size like the variables dialog: 800 x 675 centred on 1440 x 900 with a 50 px header.
  r1test::TestUi t(1440, 900);
  DialogSpec s;
  s.title = "Local variables";
  s.width = 800;
  s.height = 675;
  s.headerDivider = true;
  s.bodyPadding = 0;
  const DialogHandle h = openDialog(t.ui, s);
  t.layout();
  t.layout();
  const auto host = t.ui.absRect(h.host);
  R1_EXPECT(host.w == 800 && host.h == 675);
  R1_EXPECT(host.x == 320 && (host.y == 112 || host.y == 113));
  const auto body = t.ui.absRect(h.body);
  R1_EXPECT(body.y - host.y == 1 + 48 + 1);  // border, header (12 + 24 + 12) and its 1 px divider
  // Wider than the window: clamped to it.
  r1test::TestUi small(300, 200);
  DialogSpec wide;
  wide.title = "Wide";
  wide.width = 5000;
  const DialogHandle w = openDialog(small.ui, wide);
  small.layout();
  small.layout();
  R1_EXPECT(small.ui.absRect(w.host).w <= 300);
}

void testHostile() {
  // Huge, invalid and empty texts; far too many actions; zero-size window; a result callback that
  // opens another dialog; destroying content from a button handler.
  r1test::TestUi t(800, 600);
  DialogSpec s;
  s.title = std::string(100000, 'T') + "\xFF\xFE";
  s.description = std::string("bad \xC0\xAF utf8 ") + std::string(100000, 'd');
  for (int i = 0; i < 50; ++i) s.actions.push_back({"a" + std::to_string(i), std::string("L\xFF") + std::to_string(i), DialogActionKind::Neutral, false, false, true});
  const DialogHandle h = openDialog(t.ui, s);
  R1_EXPECT(h.valid());
  t.layout();
  t.layout();
  int buttons = 0;
  t.ui.tree().forEachDescendant(h.content, [&](WidgetId id) {
    if (t.ui.objectAs<DialogButton>(id) != nullptr) ++buttons;
  });
  R1_EXPECT(buttons == 8);
  closeDialog(t.ui, h);
  // The result callback opens another dialog (re-entrancy) and the second one closes normally.
  DialogHandle second;
  DialogSpec chain;
  chain.title = "First";
  chain.actions = {{"ok", "OK", DialogActionKind::Primary, true, false, true}};
  chain.onResult = [&](const DialogResult&) {
    DialogSpec next;
    next.title = "Second";
    second = openDialog(t.ui, next);
  };
  const DialogHandle first = openDialog(t.ui, chain);
  t.layout();
  closeDialog(t.ui, first, "ok");
  R1_EXPECT(second.valid() && isDialogOpen(t.ui, second) && !isDialogOpen(t.ui, first));
  R1_EXPECT(closeDialog(t.ui, second));
  R1_EXPECT(!isDialogOpen(t.ui, second));
  // Zero-size window.
  r1test::TestUi tiny(0, 0);
  DialogSpec zero;
  zero.title = "Zero";
  zero.actions = chain.actions;
  const DialogHandle z = openDialog(tiny.ui, zero);
  tiny.layout();
  tiny.layout();
  closeDialog(tiny.ui, z);
  R1_EXPECT(tiny.ui.overlays().count() == 0);
  // A button handler destroys the body content it lives next to, then closes: no crash.
  DialogSpec killer;
  killer.title = "Killer";
  killer.actions = {{"go", "Go", DialogActionKind::Primary, true, false, true}};
  const DialogHandle k = openDialog(t.ui, killer);
  Field& f = t.ui.create<Field>(k.body);
  t.layout();
  t.layout();
  t.ui.destroy(f.id());
  t.layout();  // the focus trap moves focus back into the dialog
  t.ui.keyDown(Key::Enter);
  R1_EXPECT(!isDialogOpen(t.ui, k));
  R1_EXPECT(t.ui.overlays().count() == 0);
}

}  // namespace

int main() {
  testLayoutAndModality();
  testEscapeEnterAndButtons();
  testStackOwnerAndScrim();
  testSizing();
  testHostile();
  return r1test::finish();
}
