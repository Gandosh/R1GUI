// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: painting of the widget tree: the depth-first walk with clipping and culling, and the look
//   of every node kind (box, text, icon, icon button, swatch, text field, title-bar glyphs).
// Why: all colours, radii and fonts are read from the resolved style of the node (StyleSheet over
//   the active theme), so nothing here knows a colour value; only shapes and placement live here.
// Callers: Scene::paint (PreviewApp frame, Bench, tests/preview offscreen render).
// Units: the tree is logical pixels, drawing is physical (physical() multiplies by the scale).
//   Text baselines are placed with the font metrics inside the widget's line box.
#include <algorithm>
#include <cmath>

#include "Scene.h"

namespace preview {

namespace layout = r1ui::core::layout;
namespace tree = r1ui::core::tree;
using r1ui::render::Color;
using r1ui::render::CornerRadii;
using r1ui::render::Painter;
using r1ui::render::Rect;

namespace {

constexpr float kSelectionAlpha = 0.4f;

bool outside(const Rect& r, int viewportWidth, int viewportHeight) {
  return r.x >= static_cast<float>(viewportWidth) || r.y >= static_cast<float>(viewportHeight) || r.x + r.w <= 0.0f ||
         r.y + r.h <= 0.0f;
}

}  // namespace

void Scene::paint(Painter& painter) {
  text_.beginFrame();
  paintWidget(painter, ids_.root);
}

void Scene::paintWidget(Painter& painter, WidgetId id) {
  tree::Widget* widget = tree_.get(id);
  if (widget == nullptr || !widget->shown()) return;
  const Rect rect = physical(widget->absRect);
  if (id != ids_.root && outside(rect, viewportWidth_, viewportHeight_)) return;
  Node* node = nodeOf(id);
  if (node != nullptr) paintNode(painter, id, *node, rect);
  const bool clips = widget->clips();
  if (clips) painter.pushClip(rect);
  for (WidgetId child = tree_.firstChild(id); child.valid(); child = tree_.nextSibling(child)) paintWidget(painter, child);
  if (clips) painter.popClip();
}

void Scene::paintBox(Painter& painter, Node& node, const Rect& rect) {
  if (node.style.empty()) return;
  const r1ui::theme::ResolvedStyle& style = resolved(node.boxCache, node.style, node.state);
  const CornerRadii radii = CornerRadii::uniform(static_cast<float>(style.radius) * scale_);
  if (style.background.a > 0) painter.fillRoundedRect(rect, radii, color(style.background));
  if (style.border.width > 0.0 && style.border.color.a > 0) {
    painter.border(rect, radii, static_cast<float>(style.border.width) * scale_, color(style.border.color));
  }
}

void Scene::paintNode(Painter& painter, WidgetId id, Node& node, const Rect& rect) {
  const float line = std::max(1.0f, std::round(scale_));
  if (node.borderTop) painter.fillRect({rect.x, rect.y, rect.w, line}, themeColor("border"));
  if (node.borderBottom) painter.fillRect({rect.x, rect.y + rect.h - line, rect.w, line}, themeColor("border"));
  const tree::Widget* widget = tree_.get(id);
  switch (node.kind) {
    case NodeKind::Box:
    case NodeKind::ModeSquare: paintBox(painter, node, rect); break;
    case NodeKind::Text: {
      paintBox(painter, node, rect);
      if (node.text.empty()) break;
      const r1ui::theme::ResolvedStyle& style =
          node.textStyle.empty() ? resolved(node.boxCache, node.style, node.state) : resolved(node.textCache, node.textStyle, r1ui::theme::State::kNone);
      const float px = static_cast<float>(style.text.fontSize) * scale_;
      const float x = rect.x + static_cast<float>(widget->style.padding[layout::kLeft]) * scale_;
      const float baseline = rect.y + text_.baselineInBox(px, rect.h);
      const bool overflow = x + text_.measure(node.text, px, style.text.weight) > rect.x + rect.w + 0.5f;
      if (overflow) painter.pushClip(rect);
      text_.draw(painter, node.text, px, style.text.weight, x, baseline, color(style.text.color));
      if (overflow) painter.popClip();
      break;
    }
    case NodeKind::Icon: {
      const r1ui::theme::ResolvedStyle& style = resolved(node.textCache, node.textStyle, r1ui::theme::State::kNone);
      const int px = std::max(1, static_cast<int>(std::lround(static_cast<float>(node.iconPx) * scale_)));
      icons_.draw(painter, node.icon, rect.x + (rect.w - static_cast<float>(px)) * 0.5f, rect.y + (rect.h - static_cast<float>(px)) * 0.5f, px,
                  color(style.text.color));
      break;
    }
    case NodeKind::IconButton: {
      paintBox(painter, node, rect);
      const r1ui::theme::ResolvedStyle& style = resolved(node.boxCache, node.style, node.state);
      const int px = std::max(1, static_cast<int>(std::lround(static_cast<float>(node.iconPx) * scale_)));
      icons_.draw(painter, node.icon, rect.x + (rect.w - static_cast<float>(px)) * 0.5f, rect.y + (rect.h - static_cast<float>(px)) * 0.5f, px,
                  color(style.text.color));
      break;
    }
    case NodeKind::Swatch: {
      const CornerRadii radii = CornerRadii::uniform(static_cast<float>(resolved(node.boxCache, node.style, 0).radius) * scale_);
      painter.fillRoundedRect(rect, radii, Color::fromHex((node.swatchRgb << 8) | 0xFFu));
      paintBox(painter, node, rect);
      break;
    }
    case NodeKind::TextField: paintTextField(painter, node, rect); break;
    case NodeKind::ChromeGlyph: {
      paintBox(painter, node, rect);
      paintChromeGlyph(painter, node, rect, color(resolved(node.boxCache, node.style, node.state).text.color));
      break;
    }
  }
}

void Scene::paintTextField(Painter& painter, Node& node, const Rect& rect) {
  paintBox(painter, node, rect);
  const r1ui::theme::ResolvedStyle& text = resolved(node.textCache, node.textStyle, r1ui::theme::State::kNone);
  const r1ui::theme::ResolvedStyle& box = resolved(node.boxCache, node.style, node.state);
  Color selection = themeColor("accent");
  selection.a = kSelectionAlpha;
  const NameFieldColors colors{color(text.text.color), themeColor("muted"), selection, color(text.text.color)};
  const float px = static_cast<float>(text.text.fontSize) * scale_;
  name_->paint(painter, rect, static_cast<float>(box.paddingX) * scale_, 6.0f * scale_, px,
               static_cast<float>(text.text.lineHeight) * scale_, node.text, colors);
}

// The four caption glyphs are drawn with lines and 1 px borders, like the OS ones.
void Scene::paintChromeGlyph(Painter& painter, const Node& node, const Rect& rect, const Color& tint) {
  const float s = scale_;
  const float stroke = std::max(1.0f, std::round(s));
  const float cx = std::round(rect.x + rect.w * 0.5f) + 0.5f * (std::fmod(stroke, 2.0f));
  const float cy = std::round(rect.y + rect.h * 0.5f) + 0.5f * (std::fmod(stroke, 2.0f));
  const float half = std::round(5.0f * s);
  const auto kind = static_cast<ChromeGlyphKind>(node.glyph);
  const CornerRadii square = CornerRadii::uniform(0.0f);
  switch (kind) {
    case ChromeGlyphKind::Minimize: painter.line(cx - half, cy, cx + half, cy, stroke, tint); break;
    case ChromeGlyphKind::Maximize: painter.border({cx - half, cy - half, 2 * half, 2 * half}, square, stroke, tint); break;
    case ChromeGlyphKind::Restore: {
      const float inset = std::round(2.0f * s);
      painter.border({cx - half, cy - half + inset, 2 * half - inset, 2 * half - inset}, square, stroke, tint);
      painter.line(cx - half + inset, cy - half + inset * 0.5f, cx + half, cy - half + inset * 0.5f, stroke, tint);
      painter.line(cx + half - stroke * 0.5f, cy - half, cx + half - stroke * 0.5f, cy + half - inset, stroke, tint);
      break;
    }
    case ChromeGlyphKind::Close:
      painter.line(cx - half, cy - half, cx + half, cy + half, stroke, tint);
      painter.line(cx - half, cy + half, cx + half, cy - half, stroke, tint);
      break;
  }
}

}  // namespace preview
