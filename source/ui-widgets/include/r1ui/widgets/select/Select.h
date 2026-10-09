// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Select, the select / combobox trigger (docs/spec/widgets.md 2.6): a 26 px panel field showing the
//   selected label (or a placeholder) and a chevron, which opens a popup list on the overlay layer;
//   the selected value API with a change callback; and the optional filter row of the combobox variant.
//   The popup list itself is SelectList.
// Why: the properties panel and dialogs choose one of a few values with this control; behaviour follows
//   spec 10 rules 52-55 (open under the trigger with at least its width, click again closes, choosing
//   changes the value, closes and runs the callback, leaving closes without change) and spec 01 (Enter,
//   Space, arrows, Home/End, type-ahead, Escape restores focus).
// Callers: application code, tests, GalleryFields. Calls: OverlayManager (popup), SelectModel (entries),
//   FieldChrome (rows, box), UiContext (focus).
// Keyboard on the trigger: Enter, Space, Up and Down open the popup; a printable character opens it and
//   starts type-ahead. Inside the popup (SelectList): Up/Down move the highlight without wrapping,
//   Home/End jump, PageUp/PageDown move a page, Enter (and Space outside search) chooses, Escape closes
//   (first clearing a non-empty filter) and focus returns to the trigger, type-ahead matches label
//   prefixes within 800 ms. The value changes only when a row is confirmed (rule 54).
// Entries: items, group labels and separators (SelectModel); disabled items are skipped by navigation
//   and cannot be chosen; the list scrolls beyond 224 px; `setCompact` selects the grouped look (11 px
//   items). Programmatic changes (setEntries, setSelectedIndex, ...) never call onChanged. Changing the
//   entries while the popup is open closes it.
// Failure behavior: more than SelectModel::kMaxEntries entries are refused (the old list stays); an
//   invalid selected index is refused; the popup closes when the select is destroyed or disabled.
#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/overlay/OverlayManager.h"
#include "r1ui/widgets/runtime/WidgetObject.h"
#include "r1ui/widgets/select/SelectModel.h"

namespace r1ui::widgets {

class Select : public WidgetObject {
 public:
  using ChangeCallback = std::function<void(size_t index, std::string_view value)>;

  Select();

  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "Select"; }

  // ---- entries (programmatic: no callbacks) ----
  bool setEntries(std::vector<SelectEntry> entries);
  // Appends one entry (O(n); use setEntries for bulk loads). False when the list is full.
  bool addItem(std::string label, std::string value = {}, bool disabled = false);
  bool addGroup(std::string label);
  bool addSeparator();
  void clearEntries();
  const std::vector<SelectEntry>& entries() const { return model_.entries(); }
  const SelectModel& model() const { return model_; }

  // ---- selection ----
  std::optional<size_t> selectedIndex() const { return selected_; }
  // nullopt clears. False (selection unchanged) for an index that is not an item.
  bool setSelectedIndex(std::optional<size_t> index);
  bool setSelectedValue(std::string_view value);
  // The selected item's value, or empty when nothing is selected.
  std::string_view selectedValue() const;
  std::string_view selectedLabel() const;

  // ---- look ----
  void setPlaceholder(std::string placeholder);
  void setCompact(bool compact);
  bool compact() const { return compact_; }
  void setSearchable(bool searchable);
  bool searchable() const { return searchable_; }
  void setSearchPlaceholder(std::string placeholder) { searchPlaceholder_ = std::move(placeholder); }
  const std::string& searchPlaceholder() const { return searchPlaceholder_; }

  void setOnChanged(ChangeCallback callback) { onChanged_ = std::move(callback); }

  // ---- popup ----
  // Opens the list under the trigger; false when disabled or the overlay layer is unavailable.
  bool open();
  void close();
  bool isOpen() const { return open_; }
  // The list widget while open (invalid otherwise).
  core::tree::WidgetId popup() const { return list_; }

  // ---- used by SelectList ----
  void applyFilter(std::string_view text);
  // A row was confirmed: selects it, closes the popup, then runs the change callback.
  void choose(size_t entryIndex);

  // ---- WidgetObject ----
  void onAttached() override;
  void onDetached() override;
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;
  std::string_view accessibleName() const override;
  void onStateChanged(uint16_t previous) override;
  void onPointerDown(Event& e) override;
  void onKeyDown(Event& e) override;
  bool wantsTextInput() const override { return true; }
  void onTextInput(Event& e) override;

 private:
  void onPopupClosed();

  SelectModel model_;
  std::optional<size_t> selected_;
  std::string placeholder_;
  std::string searchPlaceholder_ = "Search...";
  bool compact_ = false;
  bool searchable_ = false;
  bool open_ = false;
  OverlayId overlay_;
  core::tree::WidgetId list_;
  ChangeCallback onChanged_;
};

}  // namespace r1ui::widgets
