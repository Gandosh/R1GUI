// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the headless ANSI keyboard layout: unique ids, every bindable Key code on exactly one
//   cap, no overlapping caps, plausible geometry (finite, positive, inside the overall size), the
//   lookups (by key, by id, by point), modifier roles and their bits, and that unnameable caps are
//   not bindable.
// Callers: CTest (label fast).
#include <cmath>
#include <set>
#include <string>

#include "TestSupport.h"
#include "r1ui/commands/keyboard/KeyboardLayout.h"

namespace {

using namespace r1test;

void testIdsAndKeys() {
  const KeyboardLayout& layout = ansiKeyboardLayout();
  R1_EXPECT(layout.caps().size() > 80);
  std::set<std::string> ids;
  for (const KeyCap& c : layout.caps()) {
    R1_EXPECT(!c.id.empty() && !c.label.empty());
    R1_EXPECT(ids.insert(std::string(c.id)).second);  // ids are unique
    R1_EXPECT(c.bindable() == (c.key != Key::Unknown));
  }
  // Every Key the command module accepts as a chord key is on exactly one cap, and no cap carries a
  // key the module would refuse.
  int realKeys = 0;
  for (int code = 0; code < 512; ++code) {
    const Key key = static_cast<Key>(code);
    if (!isRealKey(key)) continue;
    ++realKeys;
    int count = 0;
    for (const KeyCap& c : layout.caps()) count += c.key == key ? 1 : 0;
    R1_EXPECT(count == 1);
    const KeyCap* cap = layout.findByKey(key);
    R1_EXPECT(cap != nullptr && cap->key == key);
  }
  R1_EXPECT(realKeys > 60);
  for (const KeyCap& c : layout.caps()) R1_EXPECT(c.key == Key::Unknown || isRealKey(c.key));
  R1_EXPECT(layout.findByKey(Key::Unknown) == nullptr);
  R1_EXPECT(layout.findByKey(static_cast<Key>(9999)) == nullptr);
}

void testGeometry() {
  const KeyboardLayout& layout = ansiKeyboardLayout();
  const auto& caps = layout.caps();
  for (const KeyCap& c : caps) {
    R1_EXPECT(std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.w) && std::isfinite(c.h));
    R1_EXPECT(c.x >= 0.0f && c.y >= 0.0f && c.w > 0.0f && c.h > 0.0f);
    R1_EXPECT(c.x + c.w <= layout.width() + 1e-4f && c.y + c.h <= layout.height() + 1e-4f);
  }
  for (size_t i = 0; i < caps.size(); ++i) {
    for (size_t j = i + 1; j < caps.size(); ++j) {
      const KeyCap& a = caps[i];
      const KeyCap& b = caps[j];
      const bool overlap = a.x < b.x + b.w - 1e-4f && b.x < a.x + a.w - 1e-4f && a.y < b.y + b.h - 1e-4f && b.y < a.y + a.h - 1e-4f;
      if (overlap) std::fprintf(stderr, "overlap: %s / %s\n", std::string(a.id).c_str(), std::string(b.id).c_str());
      R1_EXPECT(!overlap);
    }
  }
  // The main block rows all end at 15 units, the navigation cluster starts after it.
  for (const char* id : {"backspace", "backslash", "enter", "rshift", "rctrl"}) {
    const KeyCap* c = layout.findById(id);
    R1_EXPECT(c != nullptr && std::fabs(c->x + c->w - 15.0f) < 1e-4f);
  }
  R1_EXPECT(std::fabs(layout.width() - 18.5f) < 1e-4f && std::fabs(layout.height() - 6.5f) < 1e-4f);
}

void testLookups() {
  const KeyboardLayout& layout = ansiKeyboardLayout();
  const KeyCap* a = layout.findByKey(static_cast<Key>('A'));
  R1_EXPECT(a != nullptr && a->id == "a" && a->label == "A");
  R1_EXPECT(layout.findById("space") != nullptr && layout.findById("space")->key == Key::Space);
  R1_EXPECT(layout.findById("nope") == nullptr && layout.findById("") == nullptr);
  const KeyCap* f12 = layout.findByKey(static_cast<Key>(123));
  R1_EXPECT(f12 != nullptr && f12->id == "f12");
  const KeyCap* nine = layout.findByKey(Key::Digit0);
  R1_EXPECT(nine != nullptr && nine->id == "0");
  // Hit test: the centre of every cap finds that cap; outside the board finds none.
  for (size_t i = 0; i < layout.caps().size(); ++i) {
    const KeyCap& c = layout.caps()[i];
    R1_EXPECT(layout.capAt(c.x + c.w * 0.5f, c.y + c.h * 0.5f) == static_cast<int>(i));
  }
  R1_EXPECT(layout.capAt(-1.0f, 0.5f) == -1 && layout.capAt(100.0f, 100.0f) == -1);
  R1_EXPECT(layout.capAt(std::nanf(""), 0.5f) == -1);
}

void testModifiers() {
  const KeyboardLayout& layout = ansiKeyboardLayout();
  int shift = 0, ctrl = 0, alt = 0, meta = 0;
  for (const KeyCap& c : layout.caps()) {
    if (c.modifier == ModifierRole::None) continue;
    R1_EXPECT(!c.bindable());  // a modifier cap never carries a chord key
    shift += c.modifier == ModifierRole::Shift;
    ctrl += c.modifier == ModifierRole::Ctrl;
    alt += c.modifier == ModifierRole::Alt;
    meta += c.modifier == ModifierRole::Meta;
  }
  R1_EXPECT(shift == 2 && ctrl == 2 && alt == 2 && meta == 2);
  R1_EXPECT(modifierBit(ModifierRole::Shift) == Mod::kShift && modifierBit(ModifierRole::Ctrl) == Mod::kCtrl);
  R1_EXPECT(modifierBit(ModifierRole::Alt) == Mod::kAlt && modifierBit(ModifierRole::Meta) == Mod::kMeta);
  R1_EXPECT(modifierBit(ModifierRole::None) == 0);
  // Caps the Key enum cannot name are drawn but not bindable.
  R1_EXPECT(!layout.findById("grave")->bindable() && !layout.findById("caps")->bindable() && !layout.findById("menu")->bindable());
}

}  // namespace

int main() {
  testIdsAndKeys();
  testGeometry();
  testLookups();
  testModifiers();
  return r1test::finish();
}
