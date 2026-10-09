// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PickerDropdown, the minimal select of the pickers (the colour component mode RGB / HSL /
//   HEX and the gradient type Linear / Radial / Angular): a 26 px field-style trigger with the
//   current item and a chevron, and a list in the overlay layer.
// Why: the shared Select is being built by another group at the same time; the pickers must not
//   depend on it. After the merge an integrator may replace this private helper (the pickers use
//   only setItems, setSelected, selected and onSelect).
// Callers: ColorPicker, GradientEditor, tests. Calls: UiContext::overlays() (Menu surface, anchored
//   below the trigger, at least as wide as the trigger).
// Behaviour: click, Enter, Space, Up or Down on the focused trigger opens the list; Up / Down /
//   Home / End move the highlight, Enter or Space or a click on an item chooses it and closes the
//   list, Escape or an outside press closes it without choosing. The trigger keeps keyboard focus
//   while the list is open. Choosing an item calls onSelect(index) once (also for the item that is
//   already selected, so owners can treat it as "apply").
// Lifetime: the open list is closed when the trigger is destroyed.
#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/overlay/OverlayManager.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class PickerDropdown : public WidgetObject {
 public:
  PickerDropdown() = default;
  static std::span<const theme::StyleRuleEntry> styleRows();

  const char* typeName() const override { return "PickerDropdown"; }
  void onAttached() override;
  void onDetached() override;
  float paintOpacity() const override;
  Cursor cursor() const override { return Cursor::Pointer; }
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  std::string_view accessibleName() const override;

  void setItems(std::vector<std::string> items);
  const std::vector<std::string>& items() const { return items_; }
  // Programmatic selection (no callback); out-of-range clears the selection.
  void setSelected(int index);
  int selected() const { return selected_; }

  bool isOpen() const;
  void open();
  void close();
  int highlighted() const { return highlighted_; }
  // Called by the list items.
  void highlight(int index);
  void choose(int index);

  void onClick(Event& e) override;
  void onKeyDown(Event& e) override;

  std::function<void(int)> onSelect;

 private:
  void moveHighlight(int delta);

  std::vector<std::string> items_;
  int selected_ = -1;
  int highlighted_ = -1;
  OverlayId overlay_;
};

}  // namespace r1ui::widgets
