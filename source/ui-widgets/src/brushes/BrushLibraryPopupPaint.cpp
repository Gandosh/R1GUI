// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: painting of BrushLibraryPopup: the search field and mode switch, the category chips, the visible
//   tiles of the grid (picture or icon, quick letter badge with the typed prefix and the next letter,
//   favourite star, active outline, dimming), section headers, the scrollbar, the footer with the hint
//   line, the match count and the "pick on unique match" option, and the two popovers.
// Invariants: paint only draws (it may refresh the popup's private caches: the chip layout, the picture
//   cache); only the visible window of tiles is visited, so the cost does not depend on the library size;
//   all colours are theme tokens; nothing throws for a missing icon or picture.
// Callers: UiContext paint traversal.
#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "BrushPopupGeometry.h"
#include "r1ui/commands/brushes/BrushLetters.h"
#include "r1ui/widgets/brushes/BrushLibraryPopup.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace cb = commands::brushes;
namespace bx = brushes;
using core::layout::RectD;
using render::Color;

namespace {

Color withAlpha(Color c, float factor) {
  c.a *= factor;
  return c;
}

render::CornerRadii radius(const PaintContext& ctx, double r) { return render::CornerRadii::uniform(ctx.px(r)); }

// One line of text in a logical rectangle; returns the drawn width in physical pixels.
float line(PaintContext& ctx, std::string_view text, const RectD& r, double size, int weight, const Color& color, TextAlign align = TextAlign::Start, bool ellipsis = true) {
  theme::TextStyle style;
  style.fontSize = size;
  style.lineHeight = r.h;
  style.weight = weight;
  TextOptions options;
  options.color = color;
  options.align = align;
  options.ellipsis = ellipsis;
  return ctx.drawText(text, style, ctx.toPhysical(r.x, r.y, r.w, r.h), options);
}

// Logical width of `text` at a logical font size.
double textWidth(PaintContext& ctx, std::string_view text, double size, int weight) {
  return static_cast<double>(ctx.ui().text().measure(text, static_cast<float>(size * ctx.scale()), weight)) / ctx.scale();
}

// Greedy word wrap into at most `maxLines` lines of `room` logical pixels; the last line takes the rest (the
// caller draws it with an ellipsis when it is still too long).
std::vector<std::string> wrapWords(PaintContext& ctx, const std::string& text, double size, int weight, double room, size_t maxLines) {
  std::vector<std::string> lines;
  std::string current;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find(' ', pos);
    if (end == std::string::npos) end = text.size();
    const std::string word = text.substr(pos, end - pos);
    const std::string candidate = current.empty() ? word : current + " " + word;
    if (!current.empty() && textWidth(ctx, candidate, size, weight) > room) {
      lines.push_back(current);
      if (lines.size() + 1 >= maxLines) {
        current = text.substr(pos);
        pos = text.size();
        break;
      }
      current = word;
    } else {
      current = candidate;
    }
    pos = end + 1;
  }
  if (!current.empty()) lines.push_back(current);
  return lines;
}

// The first `count` code points of a UTF-8 string.
std::string leading(const std::string& text, size_t count) {
  size_t pos = 0;
  for (size_t i = 0; i < count && pos < text.size(); ++i) cb::decodeUtf8(text, pos);
  return text.substr(0, pos);
}

}  // namespace

void BrushLibraryPopup::paint(PaintContext& ctx) {
  drawnTiles_ = 0;
  if (chipScale_ != ctx.scale()) rebuildChips(ctx.scale());
  paintHeader(ctx);
  paintChips(ctx);
  paintGrid(ctx);
  paintFooter(ctx);
  paintPopovers(ctx);
}

// ---- header -------------------------------------------------------------------------------------

void BrushLibraryPopup::paintHeader(PaintContext& ctx) {
  render::Painter& painter = ctx.painter();
  const RectD search = searchRect();
  const render::Rect box = ctx.toPhysical(search.x, search.y, search.w, search.h);
  painter.fillRoundedRect(box, radius(ctx, 6.0), ctx.color("panel-field"));
  painter.border(box, radius(ctx, 6.0), ctx.px(1.0), ctx.color(focused() ? "accent" : "border"));
  const bool typing = mode_ == cb::QueryMode::TypeToPick;
  ctx.drawIcon(typing ? "type" : "search", 14.0, ctx.toPhysical(search.x + 8.0, search.y, 20.0, search.h), ctx.color("muted"));
  const RectD textBox{search.x + 32.0, search.y, search.w - 42.0, search.h};
  if (text_.empty()) {
    line(ctx, typing ? "Type a letter to find a brush" : "Search brush names", textBox, 13.0, 400, ctx.color("muted"));
    if (focused()) painter.fillRect(ctx.toPhysical(textBox.x, search.y + 8.0, 1.5, search.h - 16.0), ctx.color("accent"));
  } else {
    const float width = line(ctx, text_, textBox, 13.0, 400, ctx.color("surface"));
    const double caret = textBox.x + std::min(static_cast<double>(width) / ctx.scale() + 1.0, textBox.w - 2.0);
    if (focused()) painter.fillRect(ctx.toPhysical(caret, search.y + 8.0, 1.5, search.h - 16.0), ctx.color("accent"));
  }

  const RectD mode = modeRect();
  const render::Rect modeBox = ctx.toPhysical(mode.x, mode.y, mode.w, mode.h);
  painter.fillRoundedRect(modeBox, radius(ctx, 6.0), ctx.color(hoverMode_ ? "panel-field-hover" : "panel-field"));
  painter.border(modeBox, radius(ctx, 6.0), ctx.px(1.0), ctx.color("border"));
  line(ctx, typing ? "Type to pick" : "Search names", {mode.x + 10.0, mode.y, mode.w - 52.0, mode.h}, 12.0, 500, ctx.color("surface"));
  const RectD cap{mode.x + mode.w - 40.0, mode.y + 7.0, 32.0, mode.h - 14.0};
  painter.border(ctx.toPhysical(cap.x, cap.y, cap.w, cap.h), radius(ctx, 4.0), ctx.px(1.0), ctx.color("border-strong"));
  line(ctx, "Tab", cap, 10.0, 500, ctx.color("muted"), TextAlign::Center);
}

// ---- chips --------------------------------------------------------------------------------------

void BrushLibraryPopup::paintChips(PaintContext& ctx) {
  render::Painter& painter = ctx.painter();
  const bx::Box area = bx::chipsBox(widthNow());
  const RectD clip = abs(area.x, area.y, area.w, area.h);
  painter.pushClip(ctx.toPhysical(clip.x, clip.y, clip.w, clip.h));
  const size_t active = activeChip();
  for (size_t i = 0; i < chips_.size(); ++i) {
    const double x = area.x + chips_[i].x - chipScroll_;
    if (x + chips_[i].w < area.x || x > area.x + area.w) continue;
    const RectD r = abs(x, area.y + 1.0, chips_[i].w, area.h - 2.0);
    const bool selected = i == active;
    const bool hot = static_cast<int>(i) == hoverChip_;
    const render::Rect box = ctx.toPhysical(r.x, r.y, r.w, r.h);
    painter.fillRoundedRect(box, radius(ctx, r.h * 0.5), selected ? ctx.color("accent") : ctx.color(hot ? "panel-field-hover" : "panel-field"));
    if (!selected) painter.border(box, radius(ctx, r.h * 0.5), ctx.px(1.0), ctx.color("border"));
    line(ctx, chips_[i].label, r, 11.0, 500, selected ? Color{1.0f, 1.0f, 1.0f, 1.0f} : ctx.color("surface"), TextAlign::Center);
  }
  painter.popClip();
  const double total = chips_.empty() ? 0.0 : chips_.back().x + chips_.back().w;
  const auto edge = [&](double x, const char* icon) {
    const RectD r = abs(x, area.y, 18.0, area.h);
    painter.fillRect(ctx.toPhysical(r.x, r.y, r.w, r.h), ctx.color("panel"));
    ctx.drawIcon(icon, 14.0, ctx.toPhysical(r.x, r.y, r.w, r.h), ctx.color("muted"));
  };
  if (chipScroll_ > 0.5) edge(area.x, "chevron-left");
  if (chipScroll_ + area.w < total - 0.5) edge(area.x + area.w - 18.0, "chevron-right");
}

// ---- grid ---------------------------------------------------------------------------------------

void BrushLibraryPopup::pumpThumbnails(PaintContext& ctx) {
  if (!options_.thumbnails) return;
  visibleKeys_.clear();
  wantedKeys_.clear();
  bx::TileLayout::Range ranges[2];
  const int count = layout_->visible(scroll_, gridHeight(), ranges);
  std::unordered_set<uint64_t> seen;
  const auto add = [&](uint32_t tile, bool visible) {
    const uint64_t key = model_.thumbnailKey(result_.tiles[tile].brush);
    if (!seen.insert(key).second) return;
    wantedKeys_.push_back(key);
    if (visible) visibleKeys_.push_back(key);
  };
  const uint32_t total = static_cast<uint32_t>(result_.tiles.size());
  for (int r = 0; r < count; ++r) {
    for (uint32_t t = ranges[r].first; t < ranges[r].last; ++t) add(t, true);
  }
  // A margin of two rows on each side is fetched and kept so a scroll finds the pictures ready.
  const uint32_t margin = static_cast<uint32_t>(layout_->columns()) * 2;
  for (int r = 0; r < count; ++r) {
    for (uint32_t t = ranges[r].last; t < std::min(total, ranges[r].last + margin); ++t) add(t, false);
    for (uint32_t t = ranges[r].first; t-- > (ranges[r].first > margin ? ranges[r].first - margin : 0);) add(t, false);
  }
  const bool remaining = options_.thumbnails->pump(wantedKeys_, visibleKeys_, BrushThumbnailSource::bucketFor(ctx.scale()));
  if (remaining && !animating_) {
    ui().invalidator().requestAnimation(id());
    animating_ = true;
  } else if (!remaining && animating_) {
    ui().invalidator().cancelAnimation(id());
    animating_ = false;
  }
}

void BrushLibraryPopup::paintGrid(PaintContext& ctx) {
  render::Painter& painter = ctx.painter();
  const RectD grid = gridRect();
  painter.pushClip(ctx.toPhysical(grid.x, grid.y, grid.w, grid.h));
  pumpThumbnails(ctx);

  if (result_.tiles.empty()) {
    std::string title;
    std::string detail;
    if (model_.size() == 0) {
      title = "The library is empty";
    } else if (!text_.empty()) {
      title = mode_ == cb::QueryMode::TypeToPick ? "No brush starts with " + text_ : "No brush name contains " + text_;
      detail = "Backspace removes the last character.";
    } else {
      title = "No brushes in this category";
    }
    line(ctx, title, {grid.x + 20.0, grid.y + grid.h * 0.5 - 22.0, grid.w - 40.0, 20.0}, 13.0, 500, ctx.color("surface"), TextAlign::Center);
    line(ctx, detail, {grid.x + 20.0, grid.y + grid.h * 0.5 + 2.0, grid.w - 40.0, 18.0}, 11.0, 400, ctx.color("muted"), TextAlign::Center);
    painter.popClip();
    return;
  }

  // Section headers (only when the Recent section exists).
  for (int s = 0; s < 2; ++s) {
    const double top = layout_->headerTop(s);
    if (top < 0.0) continue;
    const double y = grid.y + top - scroll_;
    if (y + bx::kSectionHeader < grid.y || y > grid.y + grid.h) continue;
    line(ctx, s == 0 ? "Recent" : "All brushes", {grid.x + bx::kGridPad + 4.0, y, grid.w - 2.0 * bx::kGridPad, bx::kSectionHeader}, 11.0, 600, ctx.color("muted"));
  }

  bx::TileLayout::Range ranges[2];
  const int count = layout_->visible(scroll_, grid.h, ranges);
  for (int r = 0; r < count; ++r) {
    for (uint32_t t = ranges[r].first; t < ranges[r].last; ++t) paintTile(ctx, t, grid);
  }

  // Thin scrollbar.
  if (layout_->contentHeight() > grid.h) {
    const double content = layout_->contentHeight();
    const double thumb = std::max(24.0, grid.h * grid.h / content);
    const double range = layout_->maxScroll(grid.h);
    const double top = grid.y + (grid.h - thumb) * (range > 0.0 ? scroll_ / range : 0.0);
    painter.fillRoundedRect(ctx.toPhysical(grid.x + grid.w - bx::kScrollbarWidth - 3.0, top, bx::kScrollbarWidth, thumb), radius(ctx, 3.0), ctx.color(draggingScrollbar_ ? "muted" : "border-strong"));
  }
  painter.popClip();
}

void BrushLibraryPopup::paintTile(PaintContext& ctx, size_t tile, const RectD& grid) {
  render::Painter& painter = ctx.painter();
  const cb::TileInfo& info = result_.tiles[tile];
  const cb::BrushInfo& brush = model_.brushes()[info.brush];
  const bx::TileRect content = layout_->tileRect(static_cast<uint32_t>(tile));
  const bx::TileRect thumbContent = layout_->thumbRect(static_cast<uint32_t>(tile));
  const double ox = grid.x;
  const double oy = grid.y - scroll_;
  const RectD r{ox + content.x, oy + content.y, content.w, content.h};
  const RectD thumb{ox + thumbContent.x, oy + thumbContent.y, thumbContent.w, thumbContent.h};
  const bool hot = static_cast<int>(tile) == highlight_;
  const bool dim = !info.matched || !info.enabled;
  ++drawnTiles_;

  if (dim) painter.pushOpacity(0.4f);
  const render::Rect tileBox = ctx.toPhysical(r.x, r.y, r.w, r.h);
  if (hot) {
    painter.fillRoundedRect(tileBox, radius(ctx, 8.0), ctx.color("panel-selected-muted"));
    painter.border(tileBox, radius(ctx, 8.0), ctx.px(1.0), ctx.color("accent"));
  }
  if (info.active) painter.border(tileBox, radius(ctx, 8.0), ctx.px(2.0), ctx.color("accent"));

  // Picture, or the icon until it exists.
  const render::Rect thumbBox = ctx.toPhysical(thumb.x, thumb.y, thumb.w, thumb.h);
  painter.fillRoundedRect(thumbBox, radius(ctx, 6.0), ctx.color("panel-field"));
  const thumbs::ThumbnailTexture* picture = options_.thumbnails ? options_.thumbnails->find(model_.thumbnailKey(info.brush), BrushThumbnailSource::bucketFor(ctx.scale())) : nullptr;
  if (picture != nullptr) {
    const float tw = static_cast<float>(picture->width());
    const float th = static_cast<float>(picture->height());
    const float s = std::min(thumbBox.w / tw, thumbBox.h / th);
    const render::Rect dst{thumbBox.x + (thumbBox.w - tw * s) * 0.5f, thumbBox.y + (thumbBox.h - th * s) * 0.5f, tw * s, th * s};
    painter.pushClip(thumbBox);
    painter.drawTexture(picture->ref(), dst, {0.0f, 0.0f, 1.0f, 1.0f});
    painter.popClip();
  } else {
    ctx.drawIcon(brush.icon.empty() ? "palette" : brush.icon, 28.0, thumbBox, ctx.color("muted"), "palette");
  }

  // Quick letter badge: the shortest unique prefix; while typing, the typed part dim and the next letter marked.
  if (!dim) {
    std::string first;
    std::string next;
    bool enter = false;
    bool nextHot = false;
    const bool typing = mode_ == cb::QueryMode::TypeToPick && info.typedLength > 0;
    if (typing) {
      first = leading(model_.keyText(info.brush), info.typedLength);
      if (info.nextLetter != 0) next = cb::displayKey(std::u32string(1, info.nextLetter), 1);
      else enter = true;
      nextHot = info.nextUnique;
    } else {
      first = model_.badge(info.brush);
      enter = model_.needsEnter(info.brush);
    }
    if (!first.empty() || !next.empty()) {
      const double pad = 5.0;
      const double w1 = first.empty() ? 0.0 : textWidth(ctx, first, 10.0, 600);
      const double w2 = next.empty() ? 0.0 : textWidth(ctx, next, 10.0, 600) + 6.0;
      const double w3 = enter ? 12.0 : 0.0;
      const RectD pill{thumb.x + 4.0, thumb.y + 4.0, 2.0 * pad + w1 + w2 + w3, 16.0};
      const render::Rect pillBox = ctx.toPhysical(pill.x, pill.y, pill.w, pill.h);
      painter.fillRoundedRect(pillBox, radius(ctx, 4.0), withAlpha(ctx.color("panel"), 0.94f));
      painter.border(pillBox, radius(ctx, 4.0), ctx.px(1.0), ctx.color(nextHot ? "accent" : "border-strong"));
      double x = pill.x + pad;
      if (!first.empty()) {
        line(ctx, first, {x, pill.y, w1 + 2.0, pill.h}, 10.0, 600, ctx.color(typing ? "muted" : "surface"), TextAlign::Start, false);
        x += w1;
      }
      if (!next.empty()) {
        const RectD mark{x, pill.y + 2.0, w2, pill.h - 4.0};
        if (nextHot) painter.fillRoundedRect(ctx.toPhysical(mark.x, mark.y, mark.w, mark.h), radius(ctx, 3.0), ctx.color("accent"));
        line(ctx, next, mark, 10.0, 700, nextHot ? Color{1.0f, 1.0f, 1.0f, 1.0f} : ctx.color("surface"), TextAlign::Center, false);
        x += w2;
      }
      if (enter) ctx.drawIcon("corner-down-left", 10.0, ctx.toPhysical(x + 1.0, pill.y, 10.0, pill.h), ctx.color("muted"));
    }
  }

  // Favourite star: always for a favourite, on hover for the others.
  const bool hovered = static_cast<int>(tile) == hoverTile_;
  if (info.favourite || hovered) {
    const RectD star{thumb.x + thumb.w - 22.0, thumb.y + thumb.h - 22.0, 20.0, 20.0};
    if (hovered && hoverStar_) painter.fillRoundedRect(ctx.toPhysical(star.x, star.y, star.w, star.h), radius(ctx, 10.0), withAlpha(ctx.color("panel"), 0.9f));
    ctx.drawIcon("star", 14.0, ctx.toPhysical(star.x, star.y, star.w, star.h), info.favourite ? ctx.color("warning-action") : ctx.color("muted"));
  }

  // Name.
  line(ctx, brush.name, {r.x + 3.0, thumb.y + thumb.h + 4.0, r.w - 6.0, 18.0}, 11.0, hot ? 600 : 400, ctx.color("surface"), TextAlign::Center);
  if (dim) painter.popOpacity();
}

// ---- footer -------------------------------------------------------------------------------------

void BrushLibraryPopup::paintFooter(PaintContext& ctx) {
  render::Painter& painter = ctx.painter();
  const bx::Box foot = bx::footerBox(widthNow(), heightNow());
  const RectD f = abs(foot.x, foot.y, foot.w, foot.h);
  painter.fillRect(ctx.toPhysical(f.x, f.y, f.w, 1.0), ctx.color("border"));

  const RectD option = optionRect();
  const render::Rect optionBox = ctx.toPhysical(option.x, option.y, option.w, option.h);
  if (hoverOption_) painter.fillRoundedRect(optionBox, radius(ctx, 5.0), ctx.color("hover"));
  const bool on = model_.pickOnUniqueOption();
  const RectD check{option.x + 6.0, option.y + (option.h - 14.0) * 0.5, 14.0, 14.0};
  const render::Rect checkBox = ctx.toPhysical(check.x, check.y, check.w, check.h);
  painter.fillRoundedRect(checkBox, radius(ctx, 3.0), on ? ctx.color("accent") : ctx.color("panel-field"));
  painter.border(checkBox, radius(ctx, 3.0), ctx.px(1.0), ctx.color(on ? "accent" : "border-strong"));
  if (on) ctx.drawIcon("check", 11.0, checkBox, Color{1.0f, 1.0f, 1.0f, 1.0f});
  line(ctx, "Pick on unique match", {option.x + 26.0, option.y, option.w - 28.0, option.h}, 11.0, 400, ctx.color("surface"));

  std::string count;
  if (mode_ == cb::QueryMode::TypeToPick && !text_.empty()) count = std::to_string(result_.matchCount) + (result_.matchCount == 1 ? " match" : " matches");
  else if (!text_.empty()) count = std::to_string(result_.matchCount) + " of " + std::to_string(result_.totalCount);
  else count = std::to_string(result_.totalCount) + (result_.totalCount == 1 ? " brush" : " brushes");
  const double countW = std::ceil(textWidth(ctx, count, 11.0, 400)) + 4.0;
  line(ctx, count, {option.x - countW - 10.0, option.y, countW, option.h}, 11.0, 400, ctx.color("muted"), TextAlign::End);
  // The hint wraps over two lines beside the count and the option.
  const double hintRoom = std::max(0.0, option.x - countW - 36.0 - f.x);
  const std::vector<std::string> hintLines = wrapWords(ctx, hint(), 11.0, 400, hintRoom, 2);
  const double hintTop = f.y + (f.h - 15.0 * static_cast<double>(std::max<size_t>(1, hintLines.size()))) * 0.5;
  for (size_t i = 0; i < hintLines.size(); ++i) {
    line(ctx, hintLines[i], {f.x + 12.0, hintTop + 15.0 * static_cast<double>(i), hintRoom, 15.0}, 11.0, 400, notice_.empty() ? ctx.color("muted") : ctx.color("surface"));
  }
}

// ---- popovers -----------------------------------------------------------------------------------

void BrushLibraryPopup::paintPopovers(PaintContext& ctx) {
  render::Painter& painter = ctx.painter();
  if (menu_.open) {
    const RectD box = menuRect();
    const render::Rect pb = ctx.toPhysical(box.x, box.y, box.w, box.h);
    if (const auto layers = ctx.ui().services().tokens().shadow("md")) {
      for (auto it = layers->rbegin(); it != layers->rend(); ++it) {
        render::ShadowSpec spec;
        spec.offsetX = ctx.px(it->offsetX);
        spec.offsetY = ctx.px(it->offsetY);
        spec.blur = ctx.px(it->blur);
        spec.spread = ctx.px(it->spread);
        spec.color = ctx.color(it->color);
        painter.shadow(pb, radius(ctx, 6.0), spec);
      }
    }
    painter.fillRoundedRect(pb, radius(ctx, 6.0), ctx.color("panel"));
    painter.border(pb, radius(ctx, 6.0), ctx.px(1.0), ctx.color("border-strong"));
    for (size_t i = 0; i < menu_.labels.size(); ++i) {
      const RectD row{box.x + 4.0, box.y + 4.0 + static_cast<double>(i) * bx::kMenuRow, box.w - 8.0, bx::kMenuRow};
      const bool selected = static_cast<int>(i) == menu_.selected;
      if (selected) painter.fillRoundedRect(ctx.toPhysical(row.x, row.y, row.w, row.h), radius(ctx, 4.0), ctx.color("accent"));
      line(ctx, menu_.labels[i], {row.x + 8.0, row.y, row.w - 16.0, row.h}, 12.0, 400, selected ? Color{1.0f, 1.0f, 1.0f, 1.0f} : ctx.color("surface"));
    }
  }
  if (assign_.open && assign_.brush < model_.size()) {
    const RectD box = assignRect();
    const render::Rect pb = ctx.toPhysical(box.x, box.y, box.w, box.h);
    if (const auto layers = ctx.ui().services().tokens().shadow("md")) {
      for (auto it = layers->rbegin(); it != layers->rend(); ++it) {
        render::ShadowSpec spec;
        spec.offsetX = ctx.px(it->offsetX);
        spec.offsetY = ctx.px(it->offsetY);
        spec.blur = ctx.px(it->blur);
        spec.spread = ctx.px(it->spread);
        spec.color = ctx.color(it->color);
        painter.shadow(pb, radius(ctx, 8.0), spec);
      }
    }
    painter.fillRoundedRect(pb, radius(ctx, 8.0), ctx.color("panel"));
    painter.border(pb, radius(ctx, 8.0), ctx.px(1.0), ctx.color("accent"));
    line(ctx, "Assign a letter to " + model_.brushes()[assign_.brush].name, {box.x + 12.0, box.y + 8.0, box.w - 24.0, 20.0}, 12.0, 600, ctx.color("surface"));
    const RectD letter{box.x + 12.0, box.y + 36.0, 40.0, 40.0};
    const render::Rect lb = ctx.toPhysical(letter.x, letter.y, letter.w, letter.h);
    painter.fillRoundedRect(lb, radius(ctx, 6.0), ctx.color("panel-field"));
    painter.border(lb, radius(ctx, 6.0), ctx.px(1.5), ctx.color("accent"));
    const std::string shown = assign_.candidate != 0 ? cb::displayKey(std::u32string(1, assign_.candidate), 1) : (assign_.clear ? "-" : "?");
    line(ctx, shown, letter, 20.0, 600, ctx.color("surface"), TextAlign::Center, false);
    // The feedback sentence wraps over up to three lines of the remaining width.
    const double textX = box.x + 64.0;
    const double room = box.w - 76.0;
    const std::vector<std::string> rows = wrapWords(ctx, assign_.feedback, 11.0, 400, room, 3);
    for (size_t row = 0; row < rows.size(); ++row) {
      line(ctx, rows[row], {textX, box.y + 34.0 + 14.0 * static_cast<double>(row), room, 14.0}, 11.0, 400, ctx.color("muted"));
    }
    line(ctx, "Enter assigns  -  Delete clears  -  Esc cancels", {box.x + 12.0, box.y + box.h - 26.0, box.w - 24.0, 18.0}, 10.0, 400, ctx.color("muted"));
  }
}

// ---- the hint line ------------------------------------------------------------------------------

std::string BrushLibraryPopup::hint() const {
  if (!notice_.empty()) return notice_;
  if (assign_.open && assign_.brush < model_.size()) return "Press a letter or digit for " + model_.brushes()[assign_.brush].name + " - Enter assigns - Delete clears - Esc cancels";
  if (menu_.open) return "Up and Down choose - Enter runs - Esc closes the menu";
  const std::string closer = hooks_.openChordText.empty() ? std::string("Esc closes") : hooks_.openChordText + " or Esc closes";
  if (mode_ == cb::QueryMode::SearchAnywhere) return "Searching names - Enter picks - Tab: type to pick - Ctrl+F star - F2 letter - Esc closes";
  if (!text_.empty() && result_.tiles.empty()) return "No brush starts with " + text_ + " - Backspace widens";
  if (!text_.empty()) return "Press the marked letter to pick - Backspace widens - Enter picks - Esc closes";
  return "Type a letter - Enter picks - Tab: search - Ctrl+F star - F2 letter - " + closer;
}

}  // namespace r1ui::widgets
