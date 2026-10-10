// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the fixture of the brush library widget tests: a headless window with a command registry, router
//   and key handler wired the way an application wires them (the controller's key tap in front of the
//   CommandKeyHandler), a brush model with sample or generated brushes, the controller, and helpers that
//   drive the window with synthetic keys (a letter is key down, text input, key up, as the platform delivers
//   it), pointer input and painting into a recording Painter.
// Why: every popup test needs the same dozen objects wired the same way; this keeps each test a page of
//   expectations about what the user does and what the toolkit answers.
// Callers: tests/ui-widgets/brushes/*_test.cpp.
#pragma once

#include <cctype>
#include <memory>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/CommandRouter.h"
#include "r1ui/commands/Keymap.h"
#include "r1ui/commands/Overrides.h"
#include "r1ui/commands/brushes/BrushLibraryModel.h"
#include "r1ui/widgets/brushes/BrushLibraryController.h"
#include "r1ui/widgets/commands/CommandKeys.h"

namespace r1test {

namespace br = r1ui::commands::brushes;
using r1ui::core::events::Button;
using r1ui::core::events::Key;
namespace Mod = r1ui::core::events::Mod;
using r1ui::widgets::BrushLibraryController;
using r1ui::widgets::BrushLibraryPopup;

inline Key keyOfChar(char c) { return static_cast<Key>(std::toupper(static_cast<unsigned char>(c))); }

inline br::BrushInfo makeBrush(std::string id, std::string name, std::string category = "Sculpt") {
  br::BrushInfo info;
  info.id = std::move(id);
  info.name = std::move(name);
  info.category = std::move(category);
  info.icon = "palette";
  return info;
}

// The names of the owner's examples.
inline std::vector<br::BrushInfo> sampleBrushes() {
  const std::pair<const char*, const char*> names[] = {
      {"Standard", "Sculpt"}, {"Smooth", "Smooth"},  {"Snake Hook", "Move"},  {"Slash", "Cut"},     {"Clay Buildup", "Sculpt"}, {"Clay", "Sculpt"},   {"Inflate", "Sculpt"},
      {"Pinch", "Sculpt"},    {"Move", "Move"},      {"Blob", "Sculpt"},      {"Flatten", "Surface"}, {"Polish", "Surface"},    {"Hpolish", "Surface"}, {"Crease", "Sculpt"},
      {"Dam Standard", "Sculpt"}, {"Layer", "Surface"}, {"Trim", "Cut"},      {"Mask Pen", "Mask"},   {"Nudge", "Move"},        {"Orb Cracks", "Surface"}};
  std::vector<br::BrushInfo> list;
  for (const auto& [name, category] : names) {
    std::string id = name;
    for (char& c : id) c = c == ' ' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    list.push_back(makeBrush(id, name, category));
  }
  return list;
}

// Many brushes with names built from a small alphabet (so prefixes collide).
inline std::vector<br::BrushInfo> manyBrushes(size_t count) {
  std::vector<br::BrushInfo> list;
  uint64_t state = 12345;
  const auto next = [&] {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<size_t>(state >> 33);
  };
  static const char* const syllables[] = {"ca", "ma", "so", "ti", "ro", "ne", "lu", "pa", "ki", "do", "fe", "gu"};
  for (size_t i = 0; i < count; ++i) {
    std::string name;
    const size_t parts = 2 + next() % 3;
    for (size_t p = 0; p < parts; ++p) name += syllables[next() % 12];
    name += " " + std::to_string(i);
    list.push_back(makeBrush("m" + std::to_string(i), name, "Cat" + std::to_string(i % 6)));
  }
  return list;
}

// A widget that takes focus and records the pointer presses that reach it (what is behind the popup).
class Probe final : public r1ui::widgets::WidgetObject {
 public:
  const char* typeName() const override { return "BrushProbe"; }
  void onAttached() override {
    style().width = r1ui::core::layout::Length::percent(100.0);
    style().height = r1ui::core::layout::Length::percent(100.0);
    setFocusable(true);
  }
  void onPointerDown(Event&) override { ++downs; }
  void onKeyDown(Event&) override { ++keys; }
  int downs = 0;
  int keys = 0;
};

struct BrushFixture {
  explicit BrushFixture(std::vector<br::BrushInfo> list = sampleBrushes(), int width = 900, int height = 700)
      : t(width, height), clock(t.ui), router(registry, keymap, clock) {
    model.setBrushes(std::move(list));
    probe = &t.ui.create<Probe>(t.ui.root());
    keys = std::make_unique<r1ui::widgets::CommandKeyHandler>(t.ui, services());
    controller = std::make_unique<BrushLibraryController>(t.ui, services(), model, [this](const std::string& id) {
      picked.push_back(id);
      model.setActiveId(id);
    });
    tap = controller->tapKeys(t.ui, keys.get());
    t.ui.setGlobalKeyHandler(tap.get());
    t.layout();
  }
  ~BrushFixture() { t.ui.setGlobalKeyHandler(nullptr); }

  r1ui::widgets::CommandServices services() { return {registry, overrides, keymap, router}; }
  r1ui::widgets::UiContext& ui() { return t.ui; }
  BrushLibraryPopup* popup() { return controller->popup(); }
  bool open() { return controller->isOpen(); }

  // One key press the way the platform delivers it.
  void press(Key key, uint8_t mods = 0) {
    t.ui.keyDown(key, mods);
    t.ui.keyUp(key, mods);
  }
  // A letter or digit: key down, the character, key up.
  void type(char c, uint8_t mods = 0) {
    t.ui.keyDown(keyOfChar(c), mods);
    t.ui.textInput(static_cast<char32_t>(c), mods);
    t.ui.keyUp(keyOfChar(c), mods);
  }
  void typeText(const std::string& text) {
    for (const char c : text) type(c);
  }
  // B opens the library: the key and the character it produces.
  void pressOpen() { type('b'); }
  void movePointer(double x, double y) { t.ui.pointerMove(x, y); }
  void click(double x, double y, Button button = Button::Left) {
    t.ui.pointerMove(x, y);
    t.ui.pointerDown(x, y, button);
    t.ui.pointerUp(x, y, button);
  }
  void frame() { t.ui.frame(); }
  // Layout, then a paint into a recording Painter (the popup draws only the visible window).
  void paint() {
    t.ui.frame();
    r1ui::render::Painter painter;
    painter.begin(static_cast<uint32_t>(t.ui.viewportWidth() * t.ui.scale()), static_cast<uint32_t>(t.ui.viewportHeight() * t.ui.scale()));
    t.ui.paint(painter);
    t.ui.finishPaint();
    painter.end();
  }
  // Centre of a tile in absolute logical pixels (after a frame).
  std::pair<double, double> centreOf(size_t tile) {
    const auto r = popup()->tileRect(tile);
    return {r.x + r.w / 2.0, r.y + r.h / 2.0};
  }
  // The tile showing a brush id, or -1.
  int tileOf(const std::string& id, bool recent = false) {
    const auto index = model.indexOfId(id);
    if (!index || popup() == nullptr) return -1;
    const auto& tiles = popup()->result().tiles;
    for (size_t i = 0; i < tiles.size(); ++i) {
      if (tiles[i].brush == *index && tiles[i].recent == recent) return static_cast<int>(i);
    }
    return -1;
  }
  std::string idOfTile(int tile) {
    if (popup() == nullptr || tile < 0 || tile >= static_cast<int>(popup()->result().tiles.size())) return {};
    return model.brushes()[popup()->result().tiles[static_cast<size_t>(tile)].brush].id;
  }

  TestUi t;
  r1ui::commands::CommandRegistry registry;
  r1ui::commands::KeybindingOverrides overrides{registry};
  r1ui::commands::Keymap keymap{registry, overrides};
  r1ui::widgets::UiClock clock;
  r1ui::commands::CommandRouter router;
  br::BrushLibraryModel model;
  Probe* probe = nullptr;
  std::unique_ptr<r1ui::widgets::CommandKeyHandler> keys;
  std::unique_ptr<BrushLibraryController> controller;
  std::unique_ptr<r1ui::core::events::GlobalKeyHandler> tap;
  std::vector<std::string> picked;
};

}  // namespace r1test
