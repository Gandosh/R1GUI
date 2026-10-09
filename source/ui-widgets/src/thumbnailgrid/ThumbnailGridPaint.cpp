// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: painting of ThumbnailGrid: the visible tiles or rows (selection, hover, thumbnail or the type
//   icon on a plain tile, the type strip, the modified marker, wrapped names with the search match
//   highlighted, the type line), the keyboard cursor, the thin scrollbar and the empty / filtering
//   notices.
// Invariants: only the visible window is visited (cost independent of the item count); paint never
//   changes the tree; the thumbnail pump and the filter slices run first and only touch the widget's
//   private data; every rectangle is clipped to the widget.
// Callers: UiContext paint traversal.
#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/thumbnailgrid/ThumbnailGrid.h"

namespace r1ui::widgets {

namespace {

constexpr double kLabelPx = 11.0;
constexpr double kTypePx = 10.0;
constexpr double kStripHeight = 3.0;
constexpr double kScrollbarWidth = 6.0;
constexpr size_t kMaxWrapCache = 2048;

render::Color rgb(const color::Rgb& c, float a = 1.0f) { return {static_cast<float>(c.r), static_cast<float>(c.g), static_cast<float>(c.b), a}; }

render::Color withAlpha(render::Color c, float a) {
  c.a *= a;
  return c;
}

}  // namespace

bool ThumbnailGrid::scrollbarThumb(thumbs::Rect& track, thumbs::Rect& thumb) const {
  const thumbs::Metrics m = computeMetrics();
  const double vh = viewportHeight();
  if (!(m.contentHeight > vh) || vh <= 0.0) return false;
  const double w = ui().absRect(id()).w;
  track = {w - kScrollbarWidth - 2.0, 2.0, kScrollbarWidth, vh - 4.0};
  const double len = std::max(24.0, track.h * vh / m.contentHeight);
  const double range = m.contentHeight - vh;
  thumb = {track.x, track.y + (track.h - len) * (range > 0.0 ? scroll_ / range : 0.0), track.w, len};
  return true;
}

void ThumbnailGrid::paintScrollbar(PaintContext& ctx, const thumbs::Metrics&) {
  thumbs::Rect track;
  thumbs::Rect thumb;
  if (!scrollbarThumb(track, thumb)) return;
  const core::layout::Rect o = ui().absRect(id());
  const bool hot = draggingScrollbar_ || (pointerInside_ && pointerX_ >= thumb.x - 4.0 && pointerX_ <= thumb.x + thumb.w + 4.0 && pointerY_ >= thumb.y && pointerY_ <= thumb.y + thumb.h);
  const render::Rect r = ctx.toPhysical(o.x + thumb.x, o.y + thumb.y, thumb.w, thumb.h);
  ctx.painter().fillRoundedRect(r, render::CornerRadii::uniform(ctx.px(3.0)), ctx.color(hot ? "muted" : "border"));
}

void ThumbnailGrid::paintTile(PaintContext& ctx, const thumbs::Metrics& m, size_t shownIndex, const GridItem& item) {
  render::Painter& painter = ctx.painter();
  const core::layout::Rect o = ui().absRect(id());
  const thumbs::Rect content = thumbs::itemRect(m, shownIndex);
  const auto phys = [&](double x, double y, double w, double h) { return ctx.toPhysical(o.x + x, o.y + y - scroll_, w, h); };
  const render::Rect tile = phys(content.x, content.y, content.w, content.h);
  const bool selected = selected_.count(item.key) != 0;
  const bool hovered = shownIndex == hoverIndex_;
  const float radius = ctx.px(4.0);

  if (selected) painter.fillRoundedRect(tile, render::CornerRadii::uniform(radius), ctx.color(focused() ? "panel-selected" : "panel-selected-muted"));
  else if (hovered) painter.fillRoundedRect(tile, render::CornerRadii::uniform(radius), ctx.color("hover"));

  // ---- thumbnail square ----
  const thumbs::Rect tr = thumbs::thumbRect(m, content);
  const render::Rect thumb = phys(tr.x, tr.y, tr.w, tr.h);
  painter.fillRoundedRect(thumb, render::CornerRadii::uniform(radius), ctx.color("panel-field"));
  const thumbs::ThumbnailCache::Entry* picture = cache_ ? cache_->find(item.key, bucketFor(ctx.scale()), true) : nullptr;
  if (picture != nullptr && picture->texture && picture->texture->width() > 0 && picture->texture->height() > 0) {
    // Fit the picture into the square, keeping its aspect ratio.
    const float tw = static_cast<float>(picture->texture->width());
    const float th = static_cast<float>(picture->texture->height());
    const float s = std::min(thumb.w / tw, thumb.h / th);
    const render::Rect dst{thumb.x + (thumb.w - tw * s) * 0.5f, thumb.y + (thumb.h - th * s) * 0.5f, tw * s, th * s};
    painter.pushClip(thumb);
    painter.drawTexture(picture->texture->ref(), dst, {0.0f, 0.0f, 1.0f, 1.0f});
    painter.popClip();
  } else {
    const double iconSize = std::clamp(tr.w * 0.4, 12.0, 48.0);
    ctx.drawIcon(item.icon, iconSize, thumb, ctx.color("muted"), "file");  // "file" when the model names an icon that does not exist
  }
  // The type strip at the bottom of the thumbnail and the modified marker.
  const float strip = ctx.px(kStripHeight);
  painter.fillRect({thumb.x, thumb.y + thumb.h - strip, thumb.w, strip}, rgb(item.typeColour));
  if (item.modified) {
    const float d = ctx.px(7.0);
    painter.fillRoundedRect({thumb.x + thumb.w - d - ctx.px(4.0), thumb.y + ctx.px(4.0), d, d}, render::CornerRadii::uniform(d * 0.5f), ctx.color("warning-text"));
  }

  // ---- label ----
  TextEngine& engine = ctx.ui().text();
  const thumbs::Rect lr = thumbs::labelRect(m, content);
  const render::Rect label = phys(lr.x, lr.y, lr.w, lr.h);
  const float size = ctx.px(kLabelPx);
  const float lineH = ctx.px(14.0);
  const render::Color text = ctx.color("surface");
  const auto widthOf = [&](std::string_view s) { return engine.measure(s, size); };
  // Bisection probes many throw-away substrings: measure them without caching, and remember the result per name.
  const auto probeWidth = [&](std::string_view s) { return engine.measureTransient(s, size); };
  const auto wrapped = [&](size_t maxLines, float maxWidth) -> const std::vector<std::string>& {
    std::string key = item.name;
    key.push_back('\0');
    key += std::to_string(maxLines) + ':' + std::to_string(static_cast<long long>(maxWidth * 64.0f)) + ':' + std::to_string(static_cast<long long>(size * 64.0f));
    const auto found = wrapCache_.find(key);
    if (found != wrapCache_.end()) return found->second;
    if (wrapCache_.size() >= kMaxWrapCache) wrapCache_.clear();
    return wrapCache_.emplace(std::move(key), thumbs::wrapName(item.name, maxLines, maxWidth, probeWidth)).first->second;
  };
  const auto drawLine = [&](const std::string& line, float x, float y, const render::Color& colour) { engine.draw(painter, line, size, 400, x, y + engine.baselineInBox(size, lineH), colour); };
  const auto highlight = [&](const std::string& line, float x, float y) {
    if (search_.empty()) return;
    const size_t at = thumbs::findInsensitive(line, search_);
    if (at == std::string_view::npos) return;
    const float x0 = x + engine.measure(std::string_view(line).substr(0, at), size);
    const float w = engine.measure(std::string_view(line).substr(at, search_.size()), size);
    painter.fillRect({x0, y + ctx.px(1.0), w, lineH - ctx.px(2.0)}, withAlpha(ctx.color("accent"), 0.45f));
  };

  if (m.mode == thumbs::ViewMode::Grid) {
    const int nameLines = m.labelLines == 1 ? 1 : 2;
    const std::vector<std::string>& lines = wrapped(static_cast<size_t>(nameLines), label.w);
    painter.pushClip(label);
    float y = label.y;
    for (const std::string& line : lines) {
      const float x = label.x + std::max(0.0f, (label.w - widthOf(line)) * 0.5f);
      highlight(line, x, y);
      drawLine(line, x, y, text);
      y += lineH;
    }
    if (m.labelLines == 3 && !item.typeLabel.empty()) {
      const float tsize = ctx.px(kTypePx);
      const float w = engine.measure(item.typeLabel, tsize);
      engine.draw(painter, item.typeLabel, tsize, 400, label.x + std::max(0.0f, (label.w - w) * 0.5f), label.y + 2.0f * lineH + engine.baselineInBox(tsize, lineH), ctx.color("muted"));
    }
    painter.popClip();
  } else {
    painter.pushClip(label);
    const std::vector<std::string>& lines = wrapped(1, label.w * 0.7f);
    const float x = label.x;
    const float y = label.y + (label.h - lineH) * 0.5f;
    highlight(lines.front(), x, y);
    drawLine(lines.front(), x, y, text);
    if (!item.typeLabel.empty()) {
      const float tsize = ctx.px(kTypePx);
      const float w = engine.measure(item.typeLabel, tsize);
      engine.draw(painter, item.typeLabel, tsize, 400, label.x + label.w - w - ctx.px(6.0), y + engine.baselineInBox(tsize, lineH), ctx.color("muted"));
    }
    painter.popClip();
  }

  // The keyboard cursor.
  if (hasCursor_ && item.key == cursor_ && focusVisible()) ctx.focusRing(tile, radius);
}

void ThumbnailGrid::paint(PaintContext& ctx) {
  pumpFilter();
  const thumbs::Metrics m = computeMetrics();
  clampScroll();
  pumpThumbnails(ctx, m);
  render::Painter& painter = ctx.painter();
  const render::Rect box = ctx.box();
  painter.pushClip(box);
  GridItem item;
  const thumbs::VisibleRange vis = thumbs::visibleRange(m, scroll_, viewportHeight(), 0);
  for (size_t i = vis.first; i < vis.last; ++i) {
    fetch(i, item);
    paintTile(ctx, m, i, item);
  }
  paintScrollbar(ctx, m);

  // Notices: nothing to show, or the filter is still running (rule 49 / 50).
  TextEngine& engine = ctx.ui().text();
  const float size = ctx.px(12.0);
  if (m.itemCount == 0) {
    const std::string message = filterRunning_ ? "Searching..." : (search_.empty() && !filter_ ? "No items" : "Nothing matches");
    const float w = engine.measure(message, size);
    engine.draw(painter, message, size, 400, box.x + (box.w - w) * 0.5f, box.y + box.h * 0.4f + engine.baselineInBox(size, ctx.px(16.0)), ctx.color("muted"));
  } else if (filterRunning_) {
    const std::string message = "Filtering...";
    const float w = engine.measure(message, size);
    painter.fillRoundedRect({box.x + box.w - w - ctx.px(24.0), box.y + ctx.px(6.0), w + ctx.px(16.0), ctx.px(20.0)}, render::CornerRadii::uniform(ctx.px(4.0)), ctx.color("panel"));
    engine.draw(painter, message, size, 400, box.x + box.w - w - ctx.px(16.0), box.y + ctx.px(8.0) + engine.baselineInBox(size, ctx.px(16.0)), ctx.color("muted"));
  }
  painter.popClip();
}

void ThumbnailGrid::paintOver(PaintContext& ctx) {
  if (focusVisible() && !hasCursor_) ctx.focusRing(ctx.box(), 0.0f);
}

}  // namespace r1ui::widgets
