// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the test rig shared by the field widgets' unit tests (textinput, numberfield, select): a
//   headless UiContext over real tokens and fonts whose window host provides a scriptable clipboard
//   (it can be made to fail, throw or hold hostile text), plus synthetic input helpers (typing, key
//   chords, clicks with click counts, painting on a recording Painter).
// Why: TestSupport's TestUi has no clipboard host; the fields' copy / cut / paste rules and their
//   failure behaviour need one, and three test folders need the same input helpers.
// Callers: tests/ui-widgets/{textinput,numberfield,select}/*_test.cpp (numberfield and select include
//   it by relative path).
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"

namespace r1test {

namespace events = r1ui::core::events;

struct FieldRig {
  explicit FieldRig(int width = 400, int height = 300, float scale = 1.0f)
      : textures(), services(loadTokens(), textures, assetPaths()), ui(services, options(this)) {
    ui.setViewport(width, height, scale);
    ui.setAnimationsEnabled(false);
    ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
    ui.rootStyle().alignItems = r1ui::core::layout::Align::Start;
    ui.rootStyle().gapRow = 8.0;
    ui.rootStyle().padding[0] = ui.rootStyle().padding[1] = 10.0;
  }

  static r1ui::widgets::UiContextOptions options(FieldRig* self) {
    r1ui::widgets::UiContextOptions o;
    o.host.writeClipboard = [self](std::string_view text) {
      ++self->clipboardWrites;
      if (self->clipboardThrows) throw std::runtime_error("clipboard locked");
      self->clipboard.assign(text);
    };
    o.host.readClipboard = [self]() -> std::optional<std::string> {
      ++self->clipboardReads;
      if (self->clipboardThrows) throw std::runtime_error("clipboard locked");
      if (self->clipboardEmpty) return std::nullopt;
      return self->clipboard;
    };
    return o;
  }

  void layout() { ui.frame(); }

  // ---- synthetic input ----
  void type(std::string_view ascii) {
    for (const char c : ascii) ui.textInput(static_cast<char32_t>(static_cast<unsigned char>(c)));
  }
  bool key(events::Key k, uint8_t mods = 0) { return ui.keyDown(k, mods); }
  bool ctrl(char letter) { return ui.keyDown(static_cast<events::Key>(letter), events::Mod::kCtrl); }
  // One click at (x, y); `count` presses in quick succession give a double or triple click.
  // Each call starts a new click sequence (the clock moves on by a second first).
  void click(double x, double y, int count = 1, uint8_t mods = 0) {
    ui.setTime(ui.now() + 1000);
    for (int i = 0; i < count; ++i) {
      ui.pointerMove(x, y, mods);
      ui.pointerDown(x, y, events::Button::Left, mods);
      ui.pointerUp(x, y, events::Button::Left, mods);
    }
  }
  void tab() { ui.keyDown(events::Key::Tab); }

  // Paints once on a recording Painter; returns the number of glyph / icon quads drawn.
  uint32_t paint() {
    r1ui::render::Painter painter;
    painter.begin(static_cast<uint32_t>(ui.viewportWidth() * ui.scale()), static_cast<uint32_t>(ui.viewportHeight() * ui.scale()));
    ui.paint(painter);
    ui.finishPaint();
    painter.end();
    return painter.stats().texInstances;
  }

  r1ui::widgets::NullTextureFactory textures;
  r1ui::widgets::Services services;
  std::string clipboard;
  bool clipboardThrows = false;
  bool clipboardEmpty = false;
  int clipboardReads = 0;
  int clipboardWrites = 0;
  r1ui::widgets::UiContext ui;
};

}  // namespace r1test
