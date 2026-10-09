// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of TooltipContent.h: wrapping, measuring, painting and the registry.
// Invariants: measure() and paint() read the same cached Lines (rebuilt only when the display scale
//   changes), so what is measured is what is drawn; wrapping never loops forever on a word wider
//   than the line (it always consumes at least one code point).
// Callers: TooltipManager (through the installed factory), tests, the gallery.
#include "r1ui/widgets/tooltip/TooltipContent.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/menu/MenuModel.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

using core::tree::WidgetId;

namespace {

using theme::State::kNone;
using theme::StyleProperty;

constexpr theme::StyleRuleEntry kRows[] = {
    {"tooltip.title", kNone, StyleProperty::Foreground, "color:surface"},
    {"tooltip.title", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"tooltip.title", kNone, StyleProperty::LineHeight, "number:16"},
    {"tooltip.shortcut", kNone, StyleProperty::Foreground, "color:muted"},
    {"tooltip.shortcut", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"tooltip.shortcut", kNone, StyleProperty::LineHeight, "number:16"},
    {"tooltip.description", kNone, StyleProperty::Foreground, "color:muted"},
    {"tooltip.description", kNone, StyleProperty::FontSize, "fontSize:11"},
    {"tooltip.description", kNone, StyleProperty::LineHeight, "number:14"},
};

constexpr double kShortcutGap = 12.0;
constexpr double kDescriptionGap = 2.0;

double widthOf(UiContext& ui, std::string_view text, double fontSize, int weight) {
  if (text.empty()) return 0.0;
  const double scale = ui.scale();
  return static_cast<double>(ui.text().measure(text, static_cast<float>(fontSize * scale), weight)) / scale;
}

// Byte length of the UTF-8 sequence starting at `lead` (text is sanitised, so the lead byte is valid).
size_t sequenceLength(unsigned char lead) { return lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4; }

}  // namespace

std::vector<std::string> wrapTooltipText(UiContext& ui, std::string_view text, double fontSize, int weight, double maxWidth, size_t maxLines) {
  std::vector<std::string> lines;
  if (maxLines == 0) return lines;
  const std::string clean = sanitizeUtf8(text, kTooltipMaxTextBytes);
  const double limit = std::isfinite(maxWidth) ? std::max(1.0, maxWidth) : 1.0;
  bool dropped = false;
  const auto push = [&](std::string line) {
    if (lines.size() >= maxLines) dropped = true;
    else lines.push_back(std::move(line));
  };
  size_t pos = 0;
  while (pos <= clean.size() && !dropped) {
    // One hard-break paragraph at a time.
    size_t end = clean.find('\n', pos);
    if (end == std::string::npos) end = clean.size();
    const std::string_view paragraph(clean.data() + pos, end - pos);
    std::string line;
    size_t i = 0;
    while (i < paragraph.size() && !dropped) {
      while (i < paragraph.size() && paragraph[i] == ' ') ++i;  // runs of spaces collapse to one
      if (i >= paragraph.size()) break;
      size_t j = paragraph.find(' ', i);
      if (j == std::string_view::npos) j = paragraph.size();
      const std::string_view word = paragraph.substr(i, j - i);
      const std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
      if (widthOf(ui, candidate, fontSize, weight) <= limit) {
        line = candidate;
        i = j;
      } else if (!line.empty()) {
        push(std::move(line));  // the word starts the next line
        line.clear();
      } else {
        // A single word wider than a line: cut it after the last code point that fits (at least one).
        size_t cut = sequenceLength(static_cast<unsigned char>(word[0]));
        while (cut < word.size()) {
          const size_t step = sequenceLength(static_cast<unsigned char>(word[cut]));
          if (widthOf(ui, word.substr(0, cut + step), fontSize, weight) > limit) break;
          cut += step;
        }
        push(std::string(word.substr(0, std::min(cut, word.size()))));
        i += std::min(cut, word.size());
      }
    }
    if (!dropped && (!line.empty() || paragraph.empty())) push(std::move(line));
    if (end >= clean.size()) break;
    pos = end + 1;
  }
  if (dropped && !lines.empty()) lines.back() += "\xE2\x80\xA6";  // U+2026 horizontal ellipsis
  return lines;
}

// ---- TooltipContent -----------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> TooltipContent::styleRows() { return kRows; }

TooltipContent::TooltipContent(TooltipInfo info) : info_(std::move(info)) {
  info_.title = sanitizeUtf8(info_.title, kTooltipMaxTextBytes);
  info_.shortcut = sanitizeUtf8(info_.shortcut, kTooltipMaxTextBytes);
  info_.description = sanitizeUtf8(info_.description, kTooltipMaxTextBytes);
}

void TooltipContent::onAttached() {
  core::layout::Style& s = style();
  s.hasMeasure = true;
  s.flexShrink = 0.0;
  // The host's border has no layout effect: keep it out of the text box (box 26 px for one line).
  for (int e = 0; e < 4; ++e) s.margin[e] = core::layout::Length::px(1.0);
  node().flags.hitTestTransparent = true;
}

const TooltipContent::Lines& TooltipContent::lines() const {
  UiContext& u = ui();
  if (lines_.scale == u.scale()) return lines_;
  const theme::ResolvedStyle& ts = u.services().resolve("tooltip.title", 0);
  const theme::ResolvedStyle& ss = u.services().resolve("tooltip.shortcut", 0);
  const theme::ResolvedStyle& ds = u.services().resolve("tooltip.description", 0);
  Lines l;
  l.scale = u.scale();
  l.shortcutWidth = widthOf(u, info_.shortcut, ss.text.fontSize, ss.text.weight);
  const double titleRoom = kTooltipTitleWrapPx - (info_.shortcut.empty() ? 0.0 : l.shortcutWidth + kShortcutGap);
  l.title = wrapTooltipText(u, info_.title, ts.text.fontSize, ts.text.weight, titleRoom, kTooltipMaxLines);
  if (l.title.empty()) l.title.emplace_back();
  for (const std::string& line : l.title) l.titleWidth = std::max(l.titleWidth, widthOf(u, line, ts.text.fontSize, ts.text.weight));
  if (!info_.description.empty()) l.description = wrapTooltipText(u, info_.description, ds.text.fontSize, ds.text.weight, kTooltipDescriptionWrapPx, kTooltipMaxLines);
  for (const std::string& line : l.description) l.descriptionWidth = std::max(l.descriptionWidth, widthOf(u, line, ds.text.fontSize, ds.text.weight));
  lines_ = std::move(l);
  return lines_;
}

core::layout::MeasureResult TooltipContent::measure(const core::layout::MeasureInput&) {
  const Lines& l = lines();
  UiContext& u = ui();
  const theme::ResolvedStyle& ts = u.services().resolve("tooltip.title", 0);
  const theme::ResolvedStyle& ds = u.services().resolve("tooltip.description", 0);
  const double first = l.titleWidth + (info_.shortcut.empty() ? 0.0 : kShortcutGap + l.shortcutWidth);
  double height = ts.text.lineHeight * static_cast<double>(l.title.size());
  if (!l.description.empty()) height += kDescriptionGap + ds.text.lineHeight * static_cast<double>(l.description.size());
  return {std::max(first, l.descriptionWidth), height};
}

void TooltipContent::paint(PaintContext& ctx) {
  const Lines& l = lines();
  const theme::ResolvedStyle& ts = ctx.resolve("tooltip.title", 0);
  const theme::ResolvedStyle& ss = ctx.resolve("tooltip.shortcut", 0);
  const theme::ResolvedStyle& ds = ctx.resolve("tooltip.description", 0);
  const render::Rect box = ctx.box();
  TextOptions o;
  o.ellipsis = false;
  float y = box.y;
  const float lineH = ctx.px(ts.text.lineHeight);
  for (size_t i = 0; i < l.title.size(); ++i) {
    ctx.drawText(l.title[i], ts.text, {box.x, y, box.w, lineH}, o);
    if (i == 0 && !info_.shortcut.empty()) {
      const float x = box.x + ctx.px(l.titleWidth + kShortcutGap);
      ctx.drawText(info_.shortcut, ss.text, {x, y, box.x + box.w - x, lineH}, o);
    }
    y += lineH;
  }
  if (!l.description.empty()) {
    y += ctx.px(kDescriptionGap);
    const float descH = ctx.px(ds.text.lineHeight);
    for (const std::string& line : l.description) {
      ctx.drawText(line, ds.text, {box.x, y, box.w, descH}, o);
      y += descH;
    }
  }
}

// ---- RichTooltips -------------------------------------------------------------------------------

std::shared_ptr<RichTooltips> RichTooltips::install(UiContext& ui) {
  std::shared_ptr<RichTooltips> registry(new RichTooltips(ui));
  ui.tooltips().setTiming(kMeasuredTooltipTiming);
  ui.tooltips().setContentFactory([registry](UiContext& u, WidgetId host, WidgetId source, const std::string& text) {
    TooltipInfo info;
    if (const TooltipInfo* rich = registry->find(source)) info = *rich;
    else info.title = text;
    u.create<TooltipContent>(host, std::move(info));
    return true;
  });
  return registry;
}

void RichTooltips::purgeDead() {
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (!ui_->alive(it->first)) it = entries_.erase(it);
    else ++it;
  }
}

bool RichTooltips::set(WidgetId widget, TooltipInfo info) {
  WidgetObject* object = ui_->object(widget);
  info.title = sanitizeUtf8(info.title, kTooltipMaxTextBytes);
  info.shortcut = sanitizeUtf8(info.shortcut, kTooltipMaxTextBytes);
  info.description = sanitizeUtf8(info.description, kTooltipMaxTextBytes);
  if (object == nullptr || info.title.empty()) return false;
  if (entries_.size() >= 256) purgeDead();  // keeps the registry bounded when widgets come and go
  object->setTooltip(info.title);
  entries_[widget] = std::move(info);
  return true;
}

void RichTooltips::clear(WidgetId widget) {
  entries_.erase(widget);
  if (WidgetObject* object = ui_->object(widget)) object->setTooltip({});
}

const TooltipInfo* RichTooltips::find(WidgetId widget) const {
  const auto it = entries_.find(widget);
  return it == entries_.end() ? nullptr : &it->second;
}

}  // namespace r1ui::widgets
