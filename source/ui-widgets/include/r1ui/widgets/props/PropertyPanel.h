// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PropertyPanel, the widget that generates a property panel from a PropertyContext: a search box
//   with the advanced toggle on top, then a ScrollArea of collapsible PropertySections (one per
//   category, with a reset-category action) holding one PropertyRowView per visible property; plus
//   buildPropertyCategory(), the function that creates one such section (also used by visual tests), and
//   PropsUiClock, the props::Clock that reads the widget context's frame clock.
// Why: spec 09 rules 1-9, 52-64: the panel follows the selection, filters live on every keystroke,
//   keeps category collapse and search state in a PanelState owned by the host, refreshes within one
//   display refresh when values change anywhere (a timer of zero delay coalesces any number of change
//   notices into one refresh), and rebuilds its rows only when the set of visible rows changed.
// Layout (docs/spec/widgets.md 3): panel width is the host's (258 px in the reference), sections pad
//   their content 12 px, so rows get 234 px; fields of a two-axis row are 114 px with a 6 px gap.
// Callers: hosts (dock panels), the gallery. Calls: PropertyContext (listens to its ChangeNotifier),
//   PanelState/buildPanelModel, PropertyRowView, Section, ScrollArea, TextInput, IconButton.
// Lifetime: the context and the PanelState must outlive the panel. The panel unsubscribes when it is
//   destroyed with its UiContext alive; destroying the context first is a host error.
// Refresh rules: Values/Bindings notices refresh row widgets; Selection/Structure notices recompute the
//   model and rebuild when it differs. While an interaction is open (a scrub), the panel only refreshes
//   values and defers rebuilding, so the control under the pointer is never destroyed mid-gesture; the
//   interaction's end notifies and the rebuild follows.
// Not implemented: pinning (give each pinned panel its own PropertyContext instead), favourites, the
//   per-category advanced dropdown (one global toggle), Escape-to-clear in the search box (the clear
//   glyph clears it), remembering the value column width (no resizable divider).
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "r1ui/props/PanelState.h"
#include "r1ui/props/PropertyContext.h"
#include "r1ui/widgets/props/PropertyRow.h"
#include "r1ui/widgets/runtime/WidgetObject.h"
#include "r1ui/widgets/section/Section.h"

namespace r1ui::widgets {

class IconButton;
class TextInput;
class ScrollArea;

// Reads UiContext::now(), so history grouping follows the same clock the widgets animate on.
class PropsUiClock final : public props::Clock {
 public:
  explicit PropsUiClock(const UiContext& ui) : ui_(&ui) {}
  int64_t nowMs() const override;

 private:
  const UiContext* ui_;
};

struct PropertyPanelOptions {
  PropertyRowOptions row;
  std::string searchPlaceholder = "Search properties";
  std::string advancedTooltip = "Show advanced properties";
  std::string emptyText = "Nothing selected";
  std::string noMatchText = "No matching properties";
  std::string defaultCategory = "General";   // title of properties declared without a category
  std::string resetCategoryTooltip = "Reset category to defaults";
};

struct CategoryWidgets {
  core::tree::WidgetId section;
  core::tree::WidgetId resetAction;  // the header's reset-category button
};

// Creates the section of one category under `parent`: header (collapsible, with a reset-category action
// enabled while some row differs from its default) and one PropertyRowView per row of the category, with a
// muted caption where the sub-group changes. `onCollapsed` is called with the new collapsed state when the
// user toggles the header. `rowsOut`, when given, receives (context row, view) pairs.
CategoryWidgets buildPropertyCategory(UiContext& ui, core::tree::WidgetId parent, props::PropertyContext& context, const props::PanelCategory& category,
                                      const PropertyPanelOptions& options, std::function<void(bool collapsed)> onCollapsed = {},
                                      std::vector<std::pair<size_t, core::tree::WidgetId>>* rowsOut = nullptr);

class PropertyPanel : public WidgetObject {
 public:
  PropertyPanel(props::PropertyContext& context, props::PanelState& state, PropertyPanelOptions options = {});
  ~PropertyPanel() override;

  const char* typeName() const override { return "PropertyPanel"; }
  void onAttached() override;
  void onDetached() override;

  // Applies pending changes now: recomputes the model, rebuilds the rows if it differs, else refreshes
  // the row widgets. Called by the coalescing timer; tests and hosts may call it directly.
  void flush();
  bool dirty() const { return pending_ != Pending::None; }
  const props::PanelModel& model() const { return model_; }

  // ---- search ----
  // Sets the search text as typing does (live filter, no delay).
  void setSearchText(std::string text);
  TextInput* searchBox() const;
  IconButton* advancedButton() const;
  bool searchVisible() const { return model_.searchVisible; }

  // ---- generated parts ----
  size_t sectionCount() const { return sections_.size(); }
  PropertySection* section(size_t index) const;
  PropertySection* sectionNamed(const std::string& category) const;
  // The view of a context row, or null when it is not shown.
  PropertyRowView* rowView(size_t contextRow) const;
  ScrollArea* scrollArea() const;
  // The whole panel as text (sections, rows, states) for golden tests and diagnostics.
  std::string describe() const;

 private:
  enum class Pending : uint8_t { None, Refresh, Rebuild };

  void request(Pending level);
  void rebuild();
  void refreshRows();

  props::PropertyContext* ctx_;
  props::PanelState* state_;
  PropertyPanelOptions options_;
  props::PanelModel model_;
  bool built_ = false;
  Pending pending_ = Pending::None;
  uint32_t timer_ = 0;
  props::ChangeNotifier::Token token_ = 0;
  core::tree::WidgetId searchRow_, search_, advanced_, scroll_, notice_;
  std::vector<core::tree::WidgetId> sections_;
  std::vector<core::tree::WidgetId> resets_;   // reset-category action of each section
  std::vector<std::pair<size_t, core::tree::WidgetId>> rows_;  // context row -> view
};

}  // namespace r1ui::widgets
