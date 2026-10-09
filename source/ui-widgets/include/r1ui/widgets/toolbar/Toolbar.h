// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Toolbar, the floating tool bar of the reference (a `panel` surface with a 1 px border,
//   radius 12, padding 4 and a shadow holding 32 x 32 icon buttons with 8 px radius and 2 px gaps),
//   its ToolbarButton, flyout group (a button plus a 12 px chevron that opens a list of alternative
//   tools) and separator, horizontal or vertical, with roving keyboard navigation.
// Why: the editor's tool strip is the one place where the active tool, grouped alternatives and
//   shortcuts in tooltips ("Pen (P)") meet; the measured numbers (docs/spec/widgets.md 4.2 and the
//   toolbar screen captures) are fixed here once.
// Callers: application shells and the gallery. Calls: FlyoutList (the group flyout), ActionButton is
//   NOT used (these buttons have a different size, radius and active look).
// Button kinds: Tool (exactly one tool of the toolbar is active; the active one is accent filled with
//   a white icon and keeps that look on hover), Action (momentary), Toggle (on = `hover` fill with a
//   6 px radius, the look of the theme switch). Idle icons are `muted`, hover fills with `hover` and
//   turns the icon `surface`.
// Flyout group: the main button shows the group's current tool; the chevron opens the entries
//   (icon, label, shortcut); picking one makes it the group's tool and activates it.
// Keyboard: buttons are not in the Tab order by default (spec 01 rule 48); setKeyboardNavigation(true)
//   makes them focusable and Left / Right (Up / Down when vertical) move between them (rule 49); Space
//   and Enter activate the focused one.
// Style rows: toolbar.surface, toolbar.button, toolbar.toggle, toolbar.trigger, toolbar.separator.
// Invariants: ids of tools / actions are caller-chosen strings (duplicates are allowed for actions,
//   tool ids select the first match); callbacks may destroy the toolbar.
#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/runtime/WidgetObject.h"
#include "r1ui/widgets/toolbar/FlyoutList.h"

namespace r1ui::widgets {

enum class ToolbarOrientation : uint8_t { Horizontal, Vertical };
enum class ToolbarButtonKind : uint8_t { Tool, Action, Toggle };

// An entry of a flyout group.
struct ToolEntry {
  std::string id;
  std::string icon;
  std::string label;
  std::string shortcut;
};

class ToolbarButton : public WidgetObject {
 public:
  static constexpr double kSize = 32.0;
  static constexpr double kIconSize = 16.0;

  ToolbarButton(std::string id, std::string icon, ToolbarButtonKind kind) : id_(std::move(id)), icon_(std::move(icon)), kind_(kind) {}
  const char* typeName() const override { return "ToolbarButton"; }
  void onAttached() override;
  Cursor cursor() const override { return enabled() ? Cursor::Pointer : Cursor::Default; }
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  void onClick(Event& e) override;
  void onKeyDown(Event& e) override;
  void onKeyUp(Event& e) override;
  void onFocusOut(Event& e) override;
  void onStateChanged(uint16_t previous) override;
  uint8_t styleState() const override;
  std::string_view accessibleName() const override;

  const std::string& toolId() const { return id_; }
  const std::string& icon() const { return icon_; }
  ToolbarButtonKind kind() const { return kind_; }
  void setIcon(std::string icon);
  void setToolId(std::string id) { id_ = std::move(id); }
  bool active() const { return hasState(StateFlag::kSelected); }
  void setActive(bool on) { setSelected(on); }
  void setOnActivate(std::function<void(ToolbarButton&)> callback) { onActivate_ = std::move(callback); }
  // Runs the callback as a click would (enabled buttons only).
  bool activate();

 private:
  const char* rowKey() const { return kind_ == ToolbarButtonKind::Toggle ? "toolbar.toggle" : "toolbar.button"; }
  std::string id_;
  std::string icon_;
  ToolbarButtonKind kind_;
  bool keyPressed_ = false;
  std::function<void(ToolbarButton&)> onActivate_;
};

// Holds a group's main button and its chevron so they sit together (no gap) in the toolbar's flow.
class ToolbarGroupBox : public WidgetObject {
 public:
  explicit ToolbarGroupBox(bool vertical) : vertical_(vertical) {}
  const char* typeName() const override { return "ToolbarGroupBox"; }
  void onAttached() override;

 private:
  bool vertical_;
};

class ToolbarTrigger : public WidgetObject {
 public:
  static constexpr double kWidth = 12.0;
  explicit ToolbarTrigger(bool vertical = false) : vertical_(vertical) {}
  const char* typeName() const override { return "ToolbarTrigger"; }
  void onAttached() override;
  Cursor cursor() const override { return enabled() ? Cursor::Pointer : Cursor::Default; }
  void paint(PaintContext& ctx) override;
  void onClick(Event& e) override;
  void onKeyDown(Event& e) override;
  void setOnOpen(std::function<void(ToolbarTrigger&)> callback) { onOpen_ = std::move(callback); }
  std::string_view accessibleName() const override { return "More tools"; }

 private:
  bool vertical_;
  std::function<void(ToolbarTrigger&)> onOpen_;
};

class ToolbarSeparator : public WidgetObject {
 public:
  static constexpr double kThickness = 4.0;  // a 1 px line with 1.5 px on each side
  static constexpr double kLength = 20.0;
  explicit ToolbarSeparator(bool vertical) : vertical_(vertical) {}
  const char* typeName() const override { return "ToolbarSeparator"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;

 private:
  bool vertical_;
};

class Toolbar : public WidgetObject {
 public:
  static constexpr double kPadding = 4.0;
  static constexpr double kGap = 2.0;

  static std::span<const theme::StyleRuleEntry> styleRows();
  explicit Toolbar(ToolbarOrientation orientation = ToolbarOrientation::Horizontal) : orientation_(orientation) {}

  const char* typeName() const override { return "Toolbar"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void onKeyDown(Event& e) override;
  std::string_view accessibleName() const override { return WidgetObject::accessibleName().empty() ? std::string_view("Tools") : WidgetObject::accessibleName(); }

  ToolbarOrientation orientation() const { return orientation_; }

  // ---- content ----
  // A tool button (exactly one tool is active at a time).
  ToolbarButton& addTool(std::string id, std::string icon, std::string tooltip);
  ToolbarButton& addAction(std::string id, std::string icon, std::string tooltip, std::function<void(ToolbarButton&)> onActivate);
  ToolbarButton& addToggle(std::string id, std::string icon, std::string tooltip, std::function<void(ToolbarButton&)> onToggle);
  // A tool button with a flyout of alternatives; the first entry is the initial tool. Returns the
  // main button; an empty entry list adds a plain tool button.
  ToolbarButton& addToolGroup(std::vector<ToolEntry> entries);
  void addSeparator();

  // ---- tools ----
  // Activates the tool with this id (a group's main button matches its current entry); false when
  // unknown. Does not call the tool callback.
  bool setActiveTool(std::string_view id);
  std::string activeTool() const;
  // Called when the user activates a tool (click, key, flyout pick) with its id.
  void setOnTool(std::function<void(const std::string&)> callback) { onTool_ = std::move(callback); }
  void setFlyoutPlacement(Placement placement) { flyoutPlacement_ = placement; }
  void setKeyboardNavigation(bool enabled);
  size_t buttonCount() const { return buttons_.size(); }
  ToolbarButton* button(size_t index);

 private:
  struct Group {
    core::tree::WidgetId main;
    core::tree::WidgetId box;
    std::vector<ToolEntry> entries;
    size_t current = 0;
  };
  ToolbarButton& makeButton(core::tree::WidgetId parent, std::string id, std::string icon, std::string tooltip, ToolbarButtonKind kind);
  void toolActivated(ToolbarButton& button);
  void openGroup(size_t group);
  Group* groupOf(core::tree::WidgetId main);
  std::vector<core::tree::WidgetId> focusOrder() const;

  ToolbarOrientation orientation_;
  std::vector<core::tree::WidgetId> buttons_;   // creation order
  std::vector<core::tree::WidgetId> triggers_;  // flyout chevrons, creation order
  std::vector<Group> groups_;
  std::function<void(const std::string&)> onTool_;
  Placement flyoutPlacement_ = Placement::AboveStart;
  bool keyboard_ = false;
};

}  // namespace r1ui::widgets
