// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the property-panel anatomy of docs/spec/widgets.md section 3: PropertySection (26 px header
//   row with an 11 px semibold title, trailing 26 px action buttons, optional collapse, content
//   padded 12 px at the sides, separated from the previous section by a 1 px border that belongs to
//   the 26 px header: header + one 26 px row + 8 px bottom padding = 60 px, as measured; a header with
//   action buttons is 8 px taller and its content starts 6 px lower), PanelHeader
//   (43 px selection header: 14 px icon, 13 px semibold truncated title, trailing action buttons,
//   1 px bottom border), FieldGroup (11 px muted label, 4 px gap, control area) and FieldGrid (rows
//   of two equal columns, optionally followed by a 26 px rail, gap 6).
// Why: every properties panel is built from these parts; they fix the measured sizes, colours and
//   spacings once so panels differ only in their content.
// Callers: application panels (add controls as children of content() / control() / cell ids), the
//   gallery. Calls: Label (titles and labels), ActionButton (header actions).
// Collapse: a collapsible section toggles when its header is clicked or activated with Enter / Space
//   (the header takes focus then); the content is hidden (display none), not destroyed.
// Style rows: section.action (header icon buttons), section.panelTitle, section.panelIcon,
//   section.header (focus / hover of a collapsible header).
// Invariants: titles and labels are UTF-8 strings the Label sanitises at paint; setters ignore
//   no-op changes; every widget created here is destroyed with its owner.
#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/runtime/WidgetObject.h"
#include "r1ui/widgets/section/ActionButton.h"

namespace r1ui::widgets {

// A plain flex container (no painting): section content, grid cells, control areas.
class SectionBox : public WidgetObject {
 public:
  const char* typeName() const override { return "SectionBox"; }
  void onAttached() override;
};

// The icon or the title of a panel header: one line drawn with a style row (private leaves).
class SectionText : public WidgetObject {
 public:
  SectionText(std::string text, std::string styleKey) : text_(std::move(text)), key_(std::move(styleKey)) {}
  const char* typeName() const override { return "SectionText"; }
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  void paint(PaintContext& ctx) override;
  std::string_view accessibleName() const override { return text_; }
  void setText(std::string text);
  const std::string& text() const { return text_; }

 private:
  std::string text_;
  std::string key_;
};

class SectionIcon : public WidgetObject {
 public:
  SectionIcon(std::string icon, std::string styleKey, double size) : icon_(std::move(icon)), key_(std::move(styleKey)), size_(size) {}
  const char* typeName() const override { return "SectionIcon"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void setIcon(std::string icon);

 private:
  std::string icon_;
  std::string key_;
  double size_;
};

// The header row of a section; clickable when the section is collapsible.
class SectionHeader : public WidgetObject {
 public:
  const char* typeName() const override { return "SectionHeader"; }
  void onAttached() override;
  Cursor cursor() const override { return collapsible_ ? Cursor::Pointer : Cursor::Default; }
  void paintOver(PaintContext& ctx) override;
  void onClick(Event& e) override;
  void onKeyDown(Event& e) override;
  void onKeyUp(Event& e) override;
  void setCollapsible(bool collapsible);
  void setOnToggle(std::function<void()> callback) { onToggle_ = std::move(callback); }

 private:
  bool collapsible_ = false;
  bool keyDown_ = false;
  std::function<void()> onToggle_;
};

struct SectionOptions {
  bool collapsible = false;
  bool topBorder = true;  // the 1 px separator above the header
};

class PropertySection : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();
  explicit PropertySection(std::string title, SectionOptions options = {}) : title_(std::move(title)), options_(options) {}

  const char* typeName() const override { return "PropertySection"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  std::string_view accessibleName() const override { return WidgetObject::accessibleName().empty() ? std::string_view(title_) : WidgetObject::accessibleName(); }

  const std::string& title() const { return title_; }
  void setTitle(std::string title);
  // The widgets of the section go here.
  core::tree::WidgetId content() const { return content_; }
  core::tree::WidgetId header() const { return header_; }
  // Adds a trailing 26 x 26 icon button to the header; returns it.
  ActionButton& addAction(std::string icon, std::string tooltip, std::function<void(ActionButton&)> onActivate);
  bool collapsible() const { return options_.collapsible; }
  bool collapsed() const { return collapsed_; }
  // False (nothing changes) for a section that is not collapsible.
  bool setCollapsed(bool collapsed);
  void toggle() { setCollapsed(!collapsed_); }
  void setOnToggle(std::function<void(PropertySection&)> callback) { onToggle_ = std::move(callback); }

 private:
  std::string title_;
  SectionOptions options_;
  bool collapsed_ = false;
  core::tree::WidgetId header_;
  core::tree::WidgetId titleLabel_;
  core::tree::WidgetId spacer_;
  core::tree::WidgetId content_;
  std::function<void(PropertySection&)> onToggle_;
};

// The header of a properties panel: icon, title, actions.
class PanelHeader : public WidgetObject {
 public:
  PanelHeader(std::string title, std::string icon) : title_(std::move(title)), icon_(std::move(icon)) {}
  static std::span<const theme::StyleRuleEntry> styleRows() { return PropertySection::styleRows(); }
  const char* typeName() const override { return "PanelHeader"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  std::string_view accessibleName() const override { return WidgetObject::accessibleName().empty() ? std::string_view(title_) : WidgetObject::accessibleName(); }
  void setTitle(std::string title);
  void setIcon(std::string icon);
  const std::string& title() const { return title_; }
  ActionButton& addAction(std::string icon, std::string tooltip, std::function<void(ActionButton&)> onActivate);

 private:
  std::string title_;
  std::string icon_;
  core::tree::WidgetId iconWidget_;
  core::tree::WidgetId titleWidget_;
};

// A label above a control: add the control as a child of control().
class FieldGroup : public WidgetObject {
 public:
  explicit FieldGroup(std::string label) : label_(std::move(label)) {}
  const char* typeName() const override { return "FieldGroup"; }
  void onAttached() override;
  std::string_view accessibleName() const override { return WidgetObject::accessibleName().empty() ? std::string_view(label_) : WidgetObject::accessibleName(); }
  const std::string& label() const { return label_; }
  void setLabel(std::string label);
  core::tree::WidgetId control() const { return control_; }

 private:
  std::string label_;
  core::tree::WidgetId labelWidget_;
  core::tree::WidgetId control_;
};

// Rows of equal columns: a row of two (114 px each in a 234 px column, gap 6), or two plus a 26 px rail.
class FieldGrid : public WidgetObject {
 public:
  static constexpr double kRail = 26.0;
  static constexpr double kGap = 6.0;
  const char* typeName() const override { return "FieldGrid"; }
  void onAttached() override;

  struct Row {
    core::tree::WidgetId row;
    core::tree::WidgetId first;
    core::tree::WidgetId second;  // invalid for a one-column row
    core::tree::WidgetId rail;    // invalid unless requested
  };
  // columns: 1 or 2 (other values are clamped); rail adds the fixed 26 px column at the end.
  Row addRow(int columns = 2, bool rail = false);
};

}  // namespace r1ui::widgets
