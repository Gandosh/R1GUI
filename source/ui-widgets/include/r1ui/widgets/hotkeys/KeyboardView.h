// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: KeyboardView, the drawn keyboard of the hotkey editor: every cap of the ANSI KeyboardLayout as a
//   rounded key, coloured by whether a command is bound to it for the current modifier layer, plus a
//   legend line (Assigned Key / Unassigned Key).
// Why: the owner's reference (a hotkey editor with a keyboard that lights the keys that have a hotkey)
//   shows at a glance what is taken and what is free. The geometry is data (commands/keyboard), the
//   usage comes from collectKeyUsage, so this widget only draws, hit-tests and reports clicks.
// Callers: HotkeyEditor, tests, the gallery. Calls: KeyboardLayout, collectKeyUsage, CommandServices.
// Modifier layer: the layer shown is "exactly these modifiers held". It is the OR of two sources: the
//   toggles (clicking a Shift/Ctrl/Alt/Meta cap, or setToggledModifiers) and the physical modifiers
//   reported by the host or seen in key events (setPhysicalModifiers; the view also reads them from
//   key events it receives, the editor forwards the ones that reach it). Losing focus clears the
//   physical part.
// Colours: assigned = `accent` fill with white text; the key of the selected command additionally gets
//   a text-coloured ring; unassigned = `input` fill with muted text; caps that cannot hold a binding
//   (no Key code: punctuation, Caps Lock) are drawn at reduced opacity and ignore clicks; an active
//   modifier cap has an accent border and tint.
// Interaction: hovering a cap shows what is bound to it in the tooltip; clicking a modifier cap
//   toggles it; clicking a bindable cap reports (key, modifiers) through onKeyClicked.
// Size: the widget measures its height from its width, so the board keeps its proportions and scales
//   with the width it is given.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "r1ui/commands/keyboard/KeyUsage.h"
#include "r1ui/commands/keyboard/KeyboardLayout.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class KeyboardView final : public WidgetObject {
 public:
  explicit KeyboardView(CommandServices services) : services_(services), layout_(commands::ansiKeyboardLayout()) {}

  const char* typeName() const override { return "KeyboardView"; }
  std::string_view accessibleName() const override { return "Keyboard"; }
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  std::string_view tooltipText() const override;
  Cursor cursor() const override;
  void onPointerMove(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onClick(Event& e) override;
  void onKeyDown(Event& e) override;
  void onKeyUp(Event& e) override;
  void onFocusOut(Event& e) override;

  // ---- what is shown ----
  void setUsageFilter(commands::KeyUsageFilter filter);
  const commands::KeyUsageFilter& usageFilter() const { return filter_; }
  void setSelectedCommand(std::string commandId);
  void setToggledModifiers(uint8_t modifiers);
  void setPhysicalModifiers(uint8_t modifiers);
  uint8_t toggledModifiers() const { return toggled_; }
  uint8_t physicalModifiers() const { return physical_; }
  uint8_t effectiveModifiers() const { return static_cast<uint8_t>(toggled_ | physical_); }
  // Recomputes the usage from the keymap (call after any binding or registry change).
  void refresh();

  // ---- queries (tests, the editor) ----
  const commands::KeyboardLayout& layout() const { return layout_; }
  // The uses of a key under the current layer; empty when unassigned.
  const std::vector<commands::KeyUse>& usesOf(commands::Key key) const;
  bool assigned(commands::Key key) const { return !usesOf(key).empty(); }
  bool holdsSelected(commands::Key key) const;
  size_t assignedKeyCount() const { return usage_.size(); }
  // Window rectangle of a cap (including the visual gap).
  core::layout::RectD capRect(size_t index) const;
  // Index of the cap under the window point, or -1.
  int capAt(double x, double y) const;
  // The tooltip text of a cap under the current layer.
  std::string describe(size_t index) const;
  int hoveredCap() const { return hover_; }

  // ---- callbacks ----
  void setOnKeyClicked(std::function<void(commands::Key, uint8_t)> callback) { onKeyClicked_ = std::move(callback); }
  void setOnModifiersChanged(std::function<void(uint8_t)> callback) { onModifiersChanged_ = std::move(callback); }

 private:
  struct Metrics {
    double unit = 0.0;
    double x = 0.0;
    double y = 0.0;
  };
  Metrics metrics() const;
  void modifiersChanged();

  CommandServices services_;
  const commands::KeyboardLayout& layout_;
  commands::KeyUsageFilter filter_;
  commands::KeyUsageMap usage_;
  std::string selected_;
  uint8_t toggled_ = 0;
  uint8_t physical_ = 0;
  int hover_ = -1;
  mutable std::string tip_;
  std::function<void(commands::Key, uint8_t)> onKeyClicked_;
  std::function<void(uint8_t)> onModifiersChanged_;
};

}  // namespace r1ui::widgets
