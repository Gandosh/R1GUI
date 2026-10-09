// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: construction of the preview's widget tree: the shared builder helpers, the title bar and
//   body (SceneBuild.cpp) and the properties panel with its field kinds (PanelBuild.cpp).
// Why: building a few hundred widgets with flex styles is bulky; keeping it out of Scene.cpp keeps
//   each file about one responsibility. The builder is a friend of Scene so it can fill the node
//   table and the id registry directly; nothing else mutates structure.
// Callers: Scene constructor only. Geometry numbers come from docs/spec/widgets.md and from the
//   measured reference screen (tests/reference/openpencil/dark/screen-rectangle-selected.png):
//   panel 258 px wide, 12 px side padding, 26 px controls, 6 px gaps.
// Failure behavior: a node whose style key is missing from the sheet throws std::runtime_error.
#pragma once

#include <initializer_list>
#include <string>
#include <utility>

#include "Scene.h"

namespace preview {

// Fluent construction of a layout::Style (all values logical pixels).
class StyleBuilder {
 public:
  StyleBuilder& row() { s_.direction = r1ui::core::layout::FlexDirection::Row; return *this; }
  StyleBuilder& column() { s_.direction = r1ui::core::layout::FlexDirection::Column; return *this; }
  StyleBuilder& width(double v) { s_.width = r1ui::core::layout::Length::px(v); return *this; }
  StyleBuilder& height(double v) { s_.height = r1ui::core::layout::Length::px(v); return *this; }
  StyleBuilder& size(double w, double h) { return width(w).height(h); }
  // Equal share of the free space regardless of content (flex: 1 1 0).
  StyleBuilder& grow(double factor = 1.0) {
    s_.flexGrow = factor;
    s_.flexShrink = 1.0;
    s_.flexBasis = r1ui::core::layout::Length::px(0.0);
    return *this;
  }
  StyleBuilder& fixed() { s_.flexShrink = 0.0; return *this; }
  StyleBuilder& padding(double left, double top, double right, double bottom) {
    s_.padding[r1ui::core::layout::kLeft] = left;
    s_.padding[r1ui::core::layout::kTop] = top;
    s_.padding[r1ui::core::layout::kRight] = right;
    s_.padding[r1ui::core::layout::kBottom] = bottom;
    return *this;
  }
  StyleBuilder& gap(double v) { s_.gapColumn = v; s_.gapRow = v; return *this; }
  StyleBuilder& center() { s_.alignItems = r1ui::core::layout::Align::Center; return *this; }
  StyleBuilder& justify(r1ui::core::layout::Justify j) { s_.justifyContent = j; return *this; }
  StyleBuilder& marginTop(double v) { s_.margin[r1ui::core::layout::kTop] = r1ui::core::layout::Length::px(v); return *this; }
  StyleBuilder& marginLeft(double v) { s_.margin[r1ui::core::layout::kLeft] = r1ui::core::layout::Length::px(v); return *this; }
  StyleBuilder& marginRight(double v) { s_.margin[r1ui::core::layout::kRight] = r1ui::core::layout::Length::px(v); return *this; }
  StyleBuilder& alignEnd() { s_.alignItems = r1ui::core::layout::Align::End; return *this; }
  StyleBuilder& clip() { s_.overflow = r1ui::core::layout::Overflow::Hidden; return *this; }
  StyleBuilder& measured() { s_.hasMeasure = true; return *this; }
  r1ui::core::layout::Style build() const { return s_; }

 private:
  r1ui::core::layout::Style s_;
};

class SceneBuilder {
 public:
  explicit SceneBuilder(Scene& scene) : s_(scene) {}
  void build();

 private:
  using WidgetId = r1ui::core::tree::WidgetId;
  using Style = r1ui::core::layout::Style;

  // SceneBuild.cpp
  WidgetId add(WidgetId parent, Node node, const Style& style, bool interactive = false);
  WidgetId text(WidgetId parent, std::string value, const char* textStyle, const Style& style = {});
  void buildTitleBar(WidgetId parent);
  WidgetId chromeButton(WidgetId parent, ChromeGlyphKind glyph, const char* styleKey);

  // PanelBuild.cpp
  void buildPanel(WidgetId parent);
  void buildStrip(WidgetId parent);
  void buildHeader(WidgetId parent);
  void buildPosition(WidgetId parent);
  void buildLayoutSection(WidgetId parent);
  void buildAppearance(WidgetId parent);
  void buildFill(WidgetId parent);
  // A panel section: title row (with an optional trailing icon button) and room for content rows.
  WidgetId section(WidgetId parent, const char* title, bool borderTop, double bottomPadding,
                   const char* trailingIcon = nullptr);
  WidgetId collapsedSection(WidgetId parent, const char* title);
  // A 26 px row of controls with 6 px gaps; `marginTop` separates it from the previous row.
  WidgetId row(WidgetId parent, double marginTop);
  // A row of labelled groups (label above the control), bottom-aligned when `alignEnd`.
  WidgetId pairRow(WidgetId parent, double marginTop, bool alignEnd = false);
  WidgetId group(WidgetId parentRow, const char* label);
  // Field kinds (docs/spec/widgets.md 2.4, 2.6). `inRow` fields share the free width of a row.
  struct NumberSpec {
    const char* glyphIcon = nullptr;
    const char* glyphText = nullptr;
    const char* value = "";
    const char* suffix = nullptr;
    bool variableButton = false;
    bool chevronButton = false;
  };
  WidgetId numberField(WidgetId parent, const NumberSpec& spec, bool inRow);
  WidgetId selectField(WidgetId parent, const char* value, bool inRow, double width = 0.0);
  WidgetId iconButton(WidgetId parent, const char* icon, bool small = false);
  WidgetId iconCluster(WidgetId parent, std::initializer_list<const char*> icons);
  WidgetId segmented(WidgetId parent, std::initializer_list<const char*> items);
  WidgetId paintField(WidgetId parent);

  Scene& s_;
};

}  // namespace preview
