// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of BrushLibraryController: opening with the key and the command, the placement near the
//   pointer and inside the window, every way of closing, focus restoration, the text-field rule (the letter
//   B types B), the key that opened the library (its character and repeats are ignored), rebinding, native
//   floating windows (the library opens in the window of the key press and survives that window's
//   destruction), the keyboard isolation while open, and destruction while open.
// Callers: CTest (fast).
#include "BrushFixture.h"
#include "r1ui/commands/Conflicts.h"
#include "r1ui/widgets/textinput/TextInput.h"

using namespace r1test;
namespace rw = r1ui::widgets;
namespace rc = r1ui::commands;

namespace {

constexpr Key kF4 = static_cast<Key>(115);

bool insideWindow(const r1ui::core::layout::Rect& r, double w, double h) { return r.x >= 0 && r.y >= 0 && r.x + r.w <= w && r.y + r.h <= h; }

// A second window with its own key handler, the way the editor wires a native floating window.
struct SecondWindow {
  SecondWindow(BrushFixture& f, int w, int h) : win(w, h) {
    keys = std::make_unique<rw::CommandKeyHandler>(win.ui, f.services());
    tap = f.controller->tapKeys(win.ui, keys.get());
    win.ui.setGlobalKeyHandler(tap.get());
    probe = &win.ui.create<Probe>(win.ui.root());
    win.layout();
  }
  ~SecondWindow() { win.ui.setGlobalKeyHandler(nullptr); }
  TestUi win;
  std::unique_ptr<rw::CommandKeyHandler> keys;
  std::unique_ptr<r1ui::core::events::GlobalKeyHandler> tap;
  Probe* probe = nullptr;
};

void typeIn(TestUi& t, char c, uint8_t mods = 0) {
  t.ui.keyDown(keyOfChar(c), mods);
  t.ui.textInput(static_cast<char32_t>(c), mods);
  t.ui.keyUp(keyOfChar(c), mods);
}

void testOpenAndClose() {
  BrushFixture f;
  const rc::CommandDef* command = f.registry.find("brush.library");
  R1_EXPECT(command != nullptr && !command->description.empty() && command->context == "global");
  R1_EXPECT(f.keymap.displayText("brush.library") == "B");

  f.movePointer(450, 350);
  f.pressOpen();
  R1_EXPECT(f.open());
  BrushLibraryPopup* popup = f.popup();
  R1_EXPECT(popup != nullptr);
  if (popup == nullptr) return;
  f.frame();
  R1_EXPECT(f.ui().router().focused() == popup->id());
  R1_EXPECT(popup->text().empty());  // the character of the opening key press is not typed
  const auto rect = f.ui().absRect(popup->id());
  R1_EXPECT(rect.w == 680 && rect.h == 480);
  R1_EXPECT(std::abs(rect.x + rect.w / 2.0 - 450.0) <= 40.0 && std::abs(rect.y + rect.h / 2.0 - 350.0) <= 40.0);
  R1_EXPECT(insideWindow(rect, 900, 700));
  R1_EXPECT(f.controller->openCount() == 1 && f.controller->window() == &f.ui());

  // B again closes it (nothing typed).
  f.pressOpen();
  R1_EXPECT(!f.open() && f.controller->closeCount() == 1);
  R1_EXPECT(f.ui().overlays().count() == 0);
  R1_EXPECT(f.picked.empty());

  // Escape closes.
  f.pressOpen();
  R1_EXPECT(f.open());
  f.press(Key::Escape);
  R1_EXPECT(!f.open() && f.picked.empty());

  // A click outside closes and does not reach the widget behind.
  f.pressOpen();
  f.frame();
  const int downsBefore = f.probe->downs;
  f.click(5, 5);
  R1_EXPECT(!f.open() && f.probe->downs == downsBefore && f.picked.empty());

  // Losing the window's activation closes it (like a menu).
  f.pressOpen();
  f.ui().setWindowActive(false);
  R1_EXPECT(!f.open());
  f.ui().setWindowActive(true);

  // Menu and API invocations open in the primary window and toggle.
  R1_EXPECT(f.router.execute("brush.library", rc::ExecuteSource::Menu).isHandled());
  R1_EXPECT(f.open());
  R1_EXPECT(f.router.execute("brush.library", rc::ExecuteSource::Menu).isHandled());
  R1_EXPECT(!f.open());

  // Placement near the edges: the popup stays inside the window.
  for (const auto& p : {std::pair<double, double>{2, 2}, {898, 2}, {2, 698}, {898, 698}, {450, 2}}) {
    f.movePointer(p.first, p.second);
    f.pressOpen();
    f.frame();
    R1_EXPECT(f.open() && insideWindow(f.ui().absRect(f.popup()->id()), 900, 700));
    f.press(Key::Escape);
  }
  // The pointer outside the window: the centre.
  f.ui().pointerLeftWindow();
  f.pressOpen();
  f.frame();
  const auto r = f.ui().absRect(f.popup()->id());
  R1_EXPECT(std::abs(r.x + r.w / 2.0 - 450.0) <= 12.0 && std::abs(r.y + r.h / 2.0 - 350.0) <= 12.0);
}

void testFocus() {
  BrushFixture f;
  // Focus on a panel-like widget: the library opens and Escape gives the focus back.
  f.ui().focusWidget(f.probe->id(), r1ui::core::events::FocusReason::Pointer);
  R1_EXPECT(f.ui().router().focused() == f.probe->id());
  f.pressOpen();
  R1_EXPECT(f.open());
  R1_EXPECT(f.ui().router().focused() == f.popup()->id());
  f.press(Key::Escape);
  R1_EXPECT(!f.open() && f.ui().router().focused() == f.probe->id());

  // A text field keeps the letter B: it types B and the library stays closed.
  rw::TextInput& field = f.ui().create<rw::TextInput>(f.ui().root());
  f.frame();
  f.ui().focusWidget(field.id(), r1ui::core::events::FocusReason::Pointer);
  f.type('b');
  R1_EXPECT(!f.open());
  R1_EXPECT(field.text() == "b");
  f.type('b');
  R1_EXPECT(!f.open() && field.text() == "bb");
  f.ui().destroy(field.id());
  f.frame();
}

void testOpeningKeyIsNotTyped() {
  BrushFixture f;
  const Key kB = keyOfChar('b');
  // The platform sends key down, the character, repeats while the key is held, then key up.
  f.ui().keyDown(kB, 0);
  R1_EXPECT(f.open());
  f.ui().textInput(U'b', 0);
  f.ui().keyDown(kB, 0, true);
  f.ui().textInput(U'b', 0);
  f.ui().keyDown(kB, 0, true);
  f.ui().textInput(U'b', 0);
  R1_EXPECT(f.open() && f.popup()->text().empty());
  f.ui().keyUp(kB, 0);
  f.type('c');
  R1_EXPECT(f.popup() != nullptr && f.popup()->text() == "c");

  // Another key pressed before the release also ends the ignoring.
  BrushFixture g;
  g.ui().keyDown(kB, 0);
  g.ui().textInput(U'b', 0);
  g.type('c');
  R1_EXPECT(g.popup() != nullptr && g.popup()->text() == "c");
}

void testCloseChordRules() {
  BrushFixture f;
  f.pressOpen();
  f.type('c');
  f.type('b');  // a letter of a name now, not the closing key
  R1_EXPECT(f.open() && f.popup()->text() == "cb");
  f.press(Key::Backspace);
  f.press(Key::Backspace);
  R1_EXPECT(f.open() && f.popup()->text().empty());
  f.type('b');  // nothing typed: closes
  R1_EXPECT(!f.open());

  // Shift+B is the letter B, so brushes starting with B stay reachable (the only one picks at once).
  f.pressOpen();
  f.type('B', Mod::kShift);
  R1_EXPECT(!f.open() && f.picked == std::vector<std::string>{"blob"});

  // In the search mode B is text.
  f.pressOpen();
  f.press(Key::Tab);
  f.type('b');
  R1_EXPECT(f.open() && f.popup()->text() == "b" && f.popup()->mode() == br::QueryMode::SearchAnywhere);
  f.press(Key::Escape);

  // Rebinding: F4 opens, B does not, F4 closes even with text typed.
  const rc::AssignResult rebound = rc::assignChord(f.overrides, f.keymap, f.registry, "brush.library", 0, rc::ChordSequence::single({static_cast<Key>(115), 0, false}), false);
  R1_EXPECT(rebound.ok);
  f.pressOpen();
  R1_EXPECT(!f.open());
  f.press(kF4);
  R1_EXPECT(f.open());
  f.type('s');
  R1_EXPECT(f.open() && f.popup()->text() == "s");
  f.press(kF4);
  R1_EXPECT(!f.open());

  // Ctrl+B closes at any time as well.
  R1_EXPECT(rc::assignChord(f.overrides, f.keymap, f.registry, "brush.library", 0, rc::ChordSequence::single({keyOfChar('b'), Mod::kCtrl, false}), false).ok);
  f.press(keyOfChar('b'), Mod::kCtrl);
  R1_EXPECT(f.open());
  f.type('s');
  f.press(keyOfChar('b'), Mod::kCtrl);
  R1_EXPECT(!f.open());
  R1_EXPECT(f.keymap.displayText("brush.library") == "Ctrl+B");
}

void testNativeWindow() {
  BrushFixture f;
  {
    auto second = std::make_unique<SecondWindow>(f, 320, 260);
    typeIn(second->win, 'b');
    R1_EXPECT(f.open() && f.controller->window() == &second->win.ui);
    second->win.layout();
    R1_EXPECT(f.controller->popup() != nullptr);
    if (f.controller->popup() != nullptr) R1_EXPECT(insideWindow(second->win.ui.absRect(f.controller->popup()->id()), 320, 260));
    typeIn(second->win, 'i');  // the only brush starting with I
    R1_EXPECT(!f.open() && f.picked == std::vector<std::string>{"inflate"});

    // Pressing the key in the main window while the library is open elsewhere closes it (a toggle).
    typeIn(second->win, 'b');
    R1_EXPECT(f.open());
    f.pressOpen();
    R1_EXPECT(!f.open());

    // The window disappears while the library is open in it.
    typeIn(second->win, 'b');
    R1_EXPECT(f.open());
    const size_t closes = f.controller->closeCount();
    second.reset();
    R1_EXPECT(!f.open() && f.controller->window() == nullptr && f.controller->popup() == nullptr);
    R1_EXPECT(f.controller->closeCount() == closes + 1);
  }
  f.pressOpen();
  R1_EXPECT(f.open() && f.controller->window() == &f.ui());
  // openIn another window closes the first.
  {
    SecondWindow second(f, 400, 300);
    R1_EXPECT(f.controller->openIn(second.win.ui));
    R1_EXPECT(f.controller->window() == &second.win.ui && f.ui().overlays().count() == 0);
  }
  R1_EXPECT(!f.open());
}

void testKeyboardIsolation() {
  BrushFixture f;
  int v = 0;
  int z = 0;
  rc::CommandDef def;
  def.id = "test.v";
  def.label = "V";
  def.defaultChords = {rc::ChordSequence::single({keyOfChar('v'), 0, false}), {}};
  def.execute = [&](const rc::ExecuteArgs&) {
    ++v;
    return rc::ExecuteResult::handled();
  };
  R1_EXPECT(f.registry.add(def).ok);
  def.id = "test.z";
  def.label = "Z";
  def.defaultChords = {rc::ChordSequence::single({keyOfChar('z'), Mod::kCtrl, false}), {}};
  def.execute = [&](const rc::ExecuteArgs&) {
    ++z;
    return rc::ExecuteResult::handled();
  };
  R1_EXPECT(f.registry.add(def).ok);
  f.type('v');
  f.press(keyOfChar('z'), Mod::kCtrl);
  R1_EXPECT(v == 1 && z == 1);

  f.pressOpen();
  f.type('v');
  f.press(keyOfChar('z'), Mod::kCtrl);
  f.press(Key::F1);
  f.press(Key::Delete);
  R1_EXPECT(v == 1 && z == 1);
  R1_EXPECT(f.open());
  f.press(Key::Escape);
  f.type('v');
  R1_EXPECT(v == 2);
}

void testDestroyWhileOpen() {
  BrushFixture f;
  f.pressOpen();
  R1_EXPECT(f.open());
  f.controller.reset();
  R1_EXPECT(f.ui().overlays().count() == 0);
  R1_EXPECT(f.registry.find("brush.library") == nullptr);
  f.frame();
  f.type('b');  // nothing listens any more
  R1_EXPECT(f.ui().overlays().count() == 0);
  f.paint();

  // A new controller registers the command again.
  f.controller = std::make_unique<BrushLibraryController>(f.ui(), f.services(), f.model, [&](const std::string& id) { f.picked.push_back(id); });
  f.tap = f.controller->tapKeys(f.ui(), f.keys.get());
  f.ui().setGlobalKeyHandler(f.tap.get());
  f.pressOpen();
  R1_EXPECT(f.open());

  // The host replaces the brush list while the library is open: the popup follows.
  f.model.setBrushes({makeBrush("a", "Alpha"), makeBrush("b", "Beta"), makeBrush("c", "Gamma")});
  R1_EXPECT(f.popup()->result().tiles.size() == 3);
  f.model.setBrushes({});
  R1_EXPECT(f.popup()->result().tiles.empty() && f.popup()->highlight() == -1);
  f.paint();
  f.press(Key::Enter);  // nothing to pick
  R1_EXPECT(f.open());
  // The overlay host is destroyed by someone else: the manager notices.
  f.ui().overlays().closeAll();
  R1_EXPECT(!f.open());
}

// Random events never throw, never leave an inconsistent popup and never leak a capture.
void testFuzz() {
  BrushFixture f(manyBrushes(300));
  uint64_t state = 987654321;
  const auto next = [&](uint64_t n) {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<uint64_t>((state >> 33) % n);
  };
  const Key navigation[] = {Key::Left, Key::Right, Key::Up, Key::Down, Key::Home, Key::End, Key::PageUp, Key::PageDown};
  for (int step = 0; step < 4000; ++step) {
    switch (next(22)) {
      case 0: case 1: case 2: case 3: case 4: case 5: f.type(static_cast<char>('a' + next(26))); break;
      case 6: f.press(Key::Backspace); break;
      case 7: case 8: f.press(navigation[next(8)]); break;
      case 9: f.press(Key::Enter); break;
      case 10: f.press(Key::Tab); break;
      case 11: if (next(4) == 0) f.press(Key::Escape); break;
      case 12: f.pressOpen(); break;
      case 13: f.click(static_cast<double>(next(900)), static_cast<double>(next(700)), next(5) == 0 ? Button::Right : Button::Left); break;
      case 14: f.ui().wheel(static_cast<double>(next(900)), static_cast<double>(next(700)), 0.0, static_cast<double>(next(7)) - 3.0); break;
      case 15: f.press(keyOfChar('f'), Mod::kCtrl); break;
      case 16: f.press(static_cast<Key>(113)); break;
      case 17: f.type(static_cast<char>('0' + next(10))); break;
      case 18: f.press(Key::Delete); break;
      case 19: f.press(Key::Left, Mod::kCtrl); break;
      case 20: f.movePointer(static_cast<double>(next(900)), static_cast<double>(next(700))); break;
      default: f.paint(); break;
    }
    if (step % 25 == 0 && f.open()) {
      f.paint();
      BrushLibraryPopup* popup = f.popup();
      R1_EXPECT(popup != nullptr);
      if (popup == nullptr) continue;
      R1_EXPECT(popup->highlight() >= -1 && popup->highlight() < static_cast<int>(std::max<size_t>(1, popup->result().tiles.size())));
      R1_EXPECT(popup->scrollOffset() >= 0.0);
      R1_EXPECT(popup->drawnTiles() <= popup->result().tiles.size());
    }
  }
  R1_EXPECT(f.ui().inputFaults() == 0);
  R1_EXPECT(f.ui().router().capturer() == r1ui::core::tree::kNoWidget || !f.open());
}

}  // namespace

int main() {
  testOpenAndClose();
  testFocus();
  testOpeningKeyIsNotTyped();
  testCloseChordRules();
  testNativeWindow();
  testKeyboardIsolation();
  testDestroyWhileOpen();
  testFuzz();
  return r1test::finish();
}
