// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the shell's widget creation helper and the window structure: root, title bar (app title,
//   mode name, mode squares, minimise/maximise/close) and the body, the area below the title bar
//   that the modes fill.
// Callers: Scene constructor (via SceneBuilder::build).
#include <stdexcept>

#include "SceneBuilder.h"

namespace preview {

namespace tree = r1ui::core::tree;

namespace {

constexpr double kChromeButtonWidth = 46.0;
constexpr double kModeSquareSize = 10.0;

}  // namespace

SceneBuilder::WidgetId SceneBuilder::add(WidgetId parent, Node node, const Style& style, bool interactive) {
  for (const std::string* key : {&node.style, &node.textStyle}) {
    if (!key->empty() && !s_.sheet_.hasKey(*key)) throw std::runtime_error("The style table has no row \"" + *key + "\"");
  }
  node.interactive = interactive;
  const tree::CreateResult created = s_.tree_.create(parent);
  if (!created.ok()) throw std::runtime_error(std::string("widget tree: ") + tree::describe(created.error));
  tree::Widget* widget = s_.tree_.get(created.id);
  widget->style = style;
  widget->userData = s_.nodes_.size();
  if (interactive) widget->handler = s_.handler_.get();
  s_.nodes_.push_back(std::move(node));
  return created.id;
}

SceneBuilder::WidgetId SceneBuilder::text(WidgetId parent, std::string value, const char* textStyle, const Style& style) {
  Node node;
  node.kind = NodeKind::Text;
  node.textStyle = textStyle;
  node.text = std::move(value);
  Style st = style;
  st.hasMeasure = true;
  return add(parent, std::move(node), st);
}

SceneBuilder::WidgetId SceneBuilder::chromeButton(WidgetId parent, ChromeGlyphKind glyph, const char* styleKey) {
  Node node;
  node.kind = NodeKind::ChromeGlyph;
  node.style = styleKey;
  node.glyph = static_cast<int>(glyph);
  return add(parent, std::move(node), StyleBuilder().size(kChromeButtonWidth, kTitleBarHeight).fixed().build(), true);
}

void SceneBuilder::buildTitleBar(WidgetId bar) {
  text(bar, "R1GUI Preview", "text.app", StyleBuilder().fixed().build());
  s_.modeText_ = text(bar, "", "text.app.muted", StyleBuilder().marginLeft(8).fixed().build());
  add(bar, Node{}, StyleBuilder().grow().build());  // spacer pushes the rest to the right

  Node squares;
  const WidgetId squareRow = add(bar, std::move(squares), StyleBuilder().row().center().gap(6).marginRight(14).fixed().build());
  for (int i = 0; i < kModeCount; ++i) {
    Node square;
    square.kind = NodeKind::ModeSquare;
    square.style = "mode.square";
    square.glyph = i;
    s_.squares_[static_cast<size_t>(i)] = add(squareRow, std::move(square), StyleBuilder().size(kModeSquareSize, kModeSquareSize).fixed().build());
  }
  s_.ids_.minimize = chromeButton(bar, ChromeGlyphKind::Minimize, "chrome.button");
  s_.ids_.maximize = chromeButton(bar, ChromeGlyphKind::Maximize, "chrome.button");
  s_.ids_.close = chromeButton(bar, ChromeGlyphKind::Close, "chrome.close");
}

void SceneBuilder::build() {
  // The root takes slot 0 of the node table; its look is the canvas colour.
  Node rootNode;
  rootNode.style = "canvas";
  tree::Widget* root = s_.tree_.get(s_.ids_.root);
  root->userData = s_.nodes_.size();
  root->style = StyleBuilder().column().build();
  s_.nodes_.push_back(std::move(rootNode));

  Node bar;
  bar.style = "titlebar";
  bar.borderBottom = true;
  s_.ids_.titleBar = add(s_.ids_.root, std::move(bar), StyleBuilder().row().center().height(kTitleBarHeight).padding(12, 0, 0, 0).fixed().build());
  buildTitleBar(s_.ids_.titleBar);

  s_.ids_.body = add(s_.ids_.root, Node{}, StyleBuilder().row().grow().build());
}

}  // namespace preview
