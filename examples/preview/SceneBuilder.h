// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: construction of the shell's small widget tree (SceneBuild.cpp): the shared builder helpers
//   and the title bar with its caption buttons and mode indicator.
// Why: the builder is a friend of Scene so it can fill the node table and the id registry directly;
//   nothing else mutates structure. The tree below the title bar is the widget library's.
// Callers: Scene constructor only.
// Failure behavior: a node whose style key is missing from the sheet throws std::runtime_error.
#pragma once

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
  StyleBuilder& marginLeft(double v) { s_.margin[r1ui::core::layout::kLeft] = r1ui::core::layout::Length::px(v); return *this; }
  StyleBuilder& marginRight(double v) { s_.margin[r1ui::core::layout::kRight] = r1ui::core::layout::Length::px(v); return *this; }
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

  WidgetId add(WidgetId parent, Node node, const Style& style, bool interactive = false);
  WidgetId text(WidgetId parent, std::string value, const char* textStyle, const Style& style = {});
  void buildTitleBar(WidgetId parent);
  WidgetId chromeButton(WidgetId parent, ChromeGlyphKind glyph, const char* styleKey);

  Scene& s_;
};

}  // namespace preview
