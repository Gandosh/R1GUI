// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of SelectList.h.
// Invariants: rowTop_ always has visible().size() + 1 entries after rebuildRows(); scroll_ lies in
//   [0, maxScroll()]; highlight_ is empty or a selectable entry index of the model; the widget never
//   touches its node after asking the owner to choose (the owner destroys it).
// Callers: Select (creation), UiContext (events, layout, paint), tests.
#include "r1ui/widgets/select/SelectList.h"

#include <algorithm>
#include <cmath>

#include "r1ui/text/Utf8.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/select/Select.h"
#include "r1ui/widgets/textinput/FieldChrome.h"

namespace r1ui::widgets {

namespace {

using core::events::Button;
using core::events::Key;
namespace Mod = core::events::Mod;

constexpr double kMaxHeight = 214.0;      // 224 minus the host's 4 px padding and 1 px border on both sides
constexpr double kTextLeft = 24.0;        // measured: item text starts 24 px from the row's left edge
constexpr double kCheckLeft = 6.0;
constexpr double kCheckSize = 12.0;
constexpr double kTextRight = 12.0;
constexpr double kSearchIconLeft = 8.0;
constexpr double kSearchTextLeft = 26.0;
constexpr double kSearchRight = 8.0;
constexpr double kMinSearchWidth = 180.0;
constexpr double kWheelStep = 40.0;
constexpr double kGroupPad = 4.0;
constexpr double kSeparatorMargin = 4.0;
constexpr double kMinThumb = 20.0;
constexpr double kThumbInset = 1.0;
constexpr uint64_t kTypeAheadMs = 800;
constexpr size_t kMeasureEntries = 2000;  // widest-label scan limit for huge lists

}  // namespace

SelectList::SelectList(core::tree::WidgetId owner) : owner_(owner), filter_(text::EditorConfig{256, false}) {}

std::span<const theme::StyleRuleEntry> SelectList::styleRows() { return fieldStyleRows(); }

Select* SelectList::owner() const { return ui().objectAs<Select>(owner_); }

bool SelectList::searchable() const {
  const Select* s = owner();
  return s != nullptr && s->searchable();
}

SelectList::Metrics SelectList::metrics() const {
  const Select* s = owner();
  Metrics m;
  Services& services = ui().services();
  const theme::ResolvedStyle& item = services.resolve(s != nullptr && s->compact() ? "select.item.sm" : "select.item", 0);
  m.item = item.text.lineHeight + 2.0 * item.paddingY;
  m.group = services.resolve("select.group", 0).text.lineHeight + 2.0 * kGroupPad;
  m.separator = 1.0 + 2.0 * kSeparatorMargin;
  m.search = services.resolve("input.panel", 0).height;
  return m;
}

void SelectList::onAttached() {
  filter_.bind(ui());
  setFocusable(true);
  setWantsLayoutCallback(true);
  core::layout::Style& s = style();
  s.hasMeasure = true;
  s.flexShrink = 0.0;
  s.maxHeight = core::layout::Length::px(kMaxHeight);
  s.margin[core::layout::kLeft] = s.margin[core::layout::kRight] = s.margin[core::layout::kTop] = s.margin[core::layout::kBottom] =
      core::layout::Length::px(1.0);  // the host's border is drawn inside its padding
  rebuildRows();
  if (const Select* sel = owner()) {
    const SelectModel& model = sel->model();
    const std::optional<size_t> chosen = sel->selectedIndex();
    highlight_ = (chosen && model.rowOf(*chosen) && SelectModel::selectable(model.entries()[*chosen])) ? chosen : model.first();
  }
}

// ---- rows and scrolling ----------------------------------------------------------------------------------

void SelectList::rebuildRows() {
  rowTop_.clear();
  const Select* s = owner();
  if (s == nullptr) {
    rowTop_.push_back(0.0);
    return;
  }
  const Metrics m = metrics();
  const SelectModel& model = s->model();
  rowTop_.reserve(model.visible().size() + 1);
  double y = 0.0;
  rowTop_.push_back(y);
  for (const size_t index : model.visible()) {
    switch (model.entries()[index].kind) {
      case SelectEntryKind::Item: y += m.item; break;
      case SelectEntryKind::Group: y += m.group; break;
      case SelectEntryKind::Separator: y += m.separator; break;
    }
    rowTop_.push_back(y);
  }
}

double SelectList::viewportHeight() const {
  const double total = static_cast<double>(ui().absRect(id()).h);
  const double view = total > 0.0 ? total : kMaxHeight;
  return std::max(0.0, view - (searchable() ? metrics().search : 0.0));
}

double SelectList::maxScroll() const { return std::max(0.0, contentHeight() - viewportHeight()); }

void SelectList::setScroll(double value) {
  const double next = std::clamp(value, 0.0, maxScroll());
  if (next == scroll_) return;
  scroll_ = next;
  requestPaint();
}

void SelectList::ensureVisible(size_t entryIndex) {
  const Select* s = owner();
  if (s == nullptr) return;
  const std::optional<size_t> row = s->model().rowOf(entryIndex);
  if (!row) return;
  const double top = rowTop_[*row];
  const double bottom = rowTop_[*row + 1];
  if (top < scroll_) setScroll(top);
  else if (bottom > scroll_ + viewportHeight()) setScroll(bottom - viewportHeight());
  // A highlight on the first item reveals the group label above it too.
  if (const std::optional<size_t> first = s->model().first(); first && *first == entryIndex) setScroll(0.0);
}

std::optional<size_t> SelectList::entryAt(double localY) const {
  const Select* s = owner();
  if (s == nullptr || localY < 0.0 || rowTop_.size() < 2) return std::nullopt;
  const auto it = std::upper_bound(rowTop_.begin(), rowTop_.end(), localY);
  if (it == rowTop_.begin() || it == rowTop_.end()) return std::nullopt;
  const auto row = static_cast<size_t>(it - rowTop_.begin()) - 1;
  return s->model().visible()[row];
}

core::layout::Rect SelectList::rowRect(size_t entryIndex) const {
  const Select* s = owner();
  if (s == nullptr) return {};
  const std::optional<size_t> row = s->model().rowOf(entryIndex);
  if (!row) return {};
  const core::layout::Rect r = ui().absRect(id());
  const double searchH = searchable() ? metrics().search : 0.0;
  const double y = r.y + searchH + rowTop_[*row] - scroll_;
  return {r.x, static_cast<int32_t>(std::lround(y)), r.w, static_cast<int32_t>(std::lround(rowTop_[*row + 1] - rowTop_[*row]))};
}

void SelectList::moveHighlight(std::optional<size_t> next) {
  const Select* s = owner();
  if (next && (s == nullptr || *next >= s->model().entries().size() || !SelectModel::selectable(s->model().entries()[*next]))) return;
  if (next != highlight_) {
    highlight_ = next;
    requestPaint();
  }
  if (highlight_) ensureVisible(*highlight_);
}

void SelectList::onLayout() {
  if (!needsReveal_ || ui().absRect(id()).h <= 0) return;
  needsReveal_ = false;
  if (highlight_) ensureVisible(*highlight_);
  requestPaint();
}

void SelectList::applyFilterFromEditor() {
  Select* s = owner();
  if (s == nullptr) return;
  s->applyFilter(filter_.text());
  rebuildRows();
  scroll_ = 0.0;
  const std::optional<size_t> chosen = s->selectedIndex();
  const SelectModel& model = s->model();
  highlight_ = (chosen && model.rowOf(*chosen) && SelectModel::selectable(model.entries()[*chosen])) ? chosen : model.first();
  needsReveal_ = true;
  requestLayout();
  requestPaint();
}

// ---- measure and paint ----------------------------------------------------------------------------------------

core::layout::MeasureResult SelectList::measure(const core::layout::MeasureInput& input) {
  const Select* s = owner();
  if (s == nullptr) return {};
  const Metrics m = metrics();
  const double scale = ui().scale();
  const theme::ResolvedStyle& item = ui().services().resolve(s->compact() ? "select.item.sm" : "select.item", 0);
  const float px = static_cast<float>(item.text.fontSize * scale);
  double widest = 0.0;
  size_t counted = 0;
  for (const size_t index : s->model().visible()) {
    if (counted++ >= kMeasureEntries) break;
    const SelectEntry& e = s->model().entries()[index];
    if (e.kind == SelectEntryKind::Separator) continue;
    widest = std::max(widest, static_cast<double>(ui().text().measure(e.label, px)) / scale);
  }
  double width = kTextLeft + widest + kTextRight;
  if (searchable()) width = std::max(width, kMinSearchWidth);
  double height = contentHeight() + (s->model().visible().empty() ? m.item : 0.0) + (searchable() ? m.search : 0.0);
  if (input.widthMode == core::layout::MeasureMode::AtMost) width = std::min(width, input.width);
  if (input.heightMode == core::layout::MeasureMode::AtMost) height = std::min(height, input.height);
  return {width, height};
}

bool SelectList::thumbRect(core::layout::Rect& out) const {
  const double view = viewportHeight();
  const double content = contentHeight();
  if (view <= 0.0 || content <= view) return false;
  const theme::ResolvedStyle& bar = ui().services().resolve("select.scrollbar", 0);
  const double thumbH = std::max(kMinThumb, view * view / content);
  const double travel = view - thumbH;
  const double top = maxScroll() > 0.0 ? scroll_ / maxScroll() * travel : 0.0;
  const core::layout::Rect r = ui().absRect(id());
  const double searchH = searchable() ? metrics().search : 0.0;
  out = {static_cast<int32_t>(std::lround(r.right() - bar.height - kThumbInset)), static_cast<int32_t>(std::lround(r.y + searchH + top)),
         static_cast<int32_t>(std::lround(bar.height)), static_cast<int32_t>(std::lround(thumbH))};
  return true;
}

void SelectList::paint(PaintContext& ctx) {
  Select* s = owner();
  if (s == nullptr) return;
  const core::layout::Rect r = ctx.rect();
  const float scale = ctx.scale();
  const Metrics m = metrics();
  const bool search = s->searchable();
  const double searchH = search ? m.search : 0.0;
  const bool animate = ctx.ui().animationsActive();
  if (search && focused() && animate) ui().invalidator().requestAnimation(id());

  if (search) {
    const theme::ResolvedStyle& ss = ctx.resolve("select.search", 0);
    ctx.drawIcon("search", 12.0, ctx.toPhysical(r.x + kSearchIconLeft, r.y, 12.0, searchH), ctx.color("muted"));
    const double contentLeft = r.x + kSearchTextLeft;
    const double contentRight = static_cast<double>(r.right()) - kSearchRight;
    const float lineHeight = ctx.px(ss.text.lineHeight);
    const float lineTop = static_cast<float>(r.y + (searchH - ss.text.lineHeight) / 2.0) * scale;
    const render::Rect content{static_cast<float>(contentLeft) * scale, static_cast<float>(r.y) * scale, static_cast<float>(contentRight - contentLeft) * scale,
                               static_cast<float>(searchH) * scale};
    if (filter_.text().empty() && !s->searchPlaceholder().empty()) {
      TextOptions options;
      options.color = ctx.color("surface", 0.5);
      ctx.drawText(s->searchPlaceholder(), ss.text, {content.x, lineTop, content.w, lineHeight}, options);
    }
    LineEditorPaint lp;
    lp.content = content;
    lp.lineTop = lineTop;
    lp.lineHeight = lineHeight;
    lp.pixelSize = ctx.px(ss.text.fontSize);
    lp.weight = ss.text.weight;
    lp.text = ctx.color(ss.text.color);
    lp.selectionBackground = fieldSelectionBackground(ctx);
    lp.selectionText = fieldSelectionText(ctx);
    lp.caret = ctx.color("surface");
    lp.showCaret = focused() && filter_.caretPhaseOn(ui().now(), animate);
    filter_.paint(ctx, lp);
    ctx.painter().fillRect({static_cast<float>(r.x) * scale, static_cast<float>(r.y + searchH) * scale - ctx.hairline(), static_cast<float>(r.w) * scale, ctx.hairline()},
                           ctx.color(ss.border.color));
  }

  const SelectModel& model = s->model();
  const double viewH = viewportHeight();
  ctx.painter().pushClip(ctx.toPhysical(r.x, r.y + searchH, r.w, viewH));
  const char* itemKey = s->compact() ? "select.item.sm" : "select.item";
  if (model.visible().empty()) {
    const theme::ResolvedStyle& hint = ctx.resolve("select.hint", 0);
    TextOptions options;
    options.padLeft = kTextLeft;
    ctx.drawText(search ? "No results" : "No options", hint.text, ctx.toPhysical(r.x, r.y + searchH + m.item / 2.0 - hint.text.lineHeight / 2.0, r.w, hint.text.lineHeight), options);
  }
  // Binary search for the first visible row, then paint until the viewport ends.
  const auto firstRow = static_cast<size_t>(std::max<std::ptrdiff_t>(0, std::upper_bound(rowTop_.begin(), rowTop_.end(), scroll_) - rowTop_.begin() - 1));
  for (size_t row = firstRow; row + 1 < rowTop_.size() && rowTop_[row] - scroll_ < viewH; ++row) {
    const size_t index = model.visible()[row];
    const SelectEntry& e = model.entries()[index];
    const double y = r.y + searchH + rowTop_[row] - scroll_;
    const double h = rowTop_[row + 1] - rowTop_[row];
    switch (e.kind) {
      case SelectEntryKind::Item: {
        const bool lit = highlight_ == index && !e.disabled;
        uint8_t bits = theme::State::kNone;
        if (lit) bits |= theme::State::kHover;
        if (e.disabled) bits |= theme::State::kDisabled;
        const theme::ResolvedStyle& rs = ctx.resolve(itemKey, bits);
        if (lit) ctx.painter().fillRoundedRect(ctx.toPhysical(r.x, y, r.w, h), render::CornerRadii::uniform(ctx.px(rs.radius)), ctx.color(rs.background));
        TextOptions options;
        options.padLeft = kTextLeft;
        options.padRight = kTextRight;
        ctx.drawText(e.label, rs.text, ctx.toPhysical(r.x, y + rs.paddingY, r.w, rs.text.lineHeight), options);
        if (s->selectedIndex() == index) {
          ctx.drawIcon("check", kCheckSize, ctx.toPhysical(r.x + kCheckLeft, y + (h - kCheckSize) / 2.0, kCheckSize, kCheckSize),
                       ctx.color(ctx.resolve("select.check", 0).text.color));
        }
        break;
      }
      case SelectEntryKind::Group: {
        const theme::ResolvedStyle& gs = ctx.resolve("select.group", 0);
        TextOptions options;
        options.padLeft = kTextLeft;
        options.padRight = kTextRight;
        ctx.drawText(e.label, gs.text, ctx.toPhysical(r.x, y + kGroupPad, r.w, gs.text.lineHeight), options);
        break;
      }
      case SelectEntryKind::Separator: {
        const theme::ResolvedStyle& sp = ctx.resolve("select.separator", 0);
        ctx.painter().fillRect({static_cast<float>(r.x) * scale, static_cast<float>(y + kSeparatorMargin) * scale, static_cast<float>(r.w) * scale, ctx.hairline()},
                               ctx.color(sp.border.color));
        break;
      }
    }
  }
  ctx.painter().popClip();

  // The reference list shows no scrollbar until the pointer is over it (it is an overlay bar).
  core::layout::Rect thumb;
  if ((hovered() || thumbDrag_) && thumbRect(thumb)) {
    const theme::ResolvedStyle& bar = ctx.resolve("select.scrollbar", thumbHover_ || thumbDrag_ ? theme::State::kHover : theme::State::kNone);
    ctx.painter().fillRoundedRect(ctx.toPhysical(thumb.x, thumb.y, thumb.w, thumb.h), render::CornerRadii::uniform(ctx.px(bar.radius)), ctx.color(bar.background));
  }
}

Cursor SelectList::cursor() const {
  if (searchable() && ui().pointerKnown()) {
    const core::layout::Rect r = ui().absRect(id());
    if (ui().pointerY() < r.y + metrics().search) return Cursor::Text;
  }
  return Cursor::Default;
}

// ---- pointer ----------------------------------------------------------------------------------------------------

void SelectList::onPointerDown(Event& e) {
  if (e.button != Button::Left) return;
  e.markHandled();
  core::layout::Rect thumb;
  if (thumbRect(thumb) && core::layout::containsPoint(thumb, e.x, e.y)) {
    thumbDrag_ = true;
    thumbGrab_ = e.y - thumb.y;
    ui().router().capturePointer(id());
    requestPaint();
    return;
  }
  if (searchable() && e.localY < metrics().search) {
    filterDragging_ = true;
    ui().router().capturePointer(id());
    const double scale = ui().scale();
    filter_.ensureLayout(static_cast<float>(ui().services().resolve("select.search", 0).text.fontSize * scale));
    const int clicks = static_cast<int>(std::clamp<uint32_t>(e.clickCount, 1, 3));
    filter_.pointerPress(static_cast<float>((e.localX - kSearchTextLeft) * scale), clicks, (e.modifiers & Mod::kShift) != 0);
    requestPaint();
  }
}

void SelectList::onPointerMove(Event& e) {
  if (thumbDrag_) {
    const core::layout::Rect r = ui().absRect(id());
    const double view = viewportHeight();
    const double thumbH = std::max(kMinThumb, view * view / std::max(contentHeight(), 1.0));
    const double travel = std::max(1.0, view - thumbH);
    const double top = e.y - thumbGrab_ - (r.y + (searchable() ? metrics().search : 0.0));
    setScroll(top / travel * maxScroll());
    return;
  }
  if (filterDragging_) {
    filter_.pointerDrag(static_cast<float>((e.localX - kSearchTextLeft) * ui().scale()));
    requestPaint();
    return;
  }
  core::layout::Rect thumb;
  const bool overThumb = thumbRect(thumb) && core::layout::containsPoint(thumb, e.x, e.y);
  if (overThumb != thumbHover_) {
    thumbHover_ = overThumb;
    requestPaint();
  }
  const double searchH = searchable() ? metrics().search : 0.0;
  if (e.localY < searchH || overThumb) return;
  if (const std::optional<size_t> entry = entryAt(e.localY - searchH + scroll_)) {
    const Select* s = owner();
    if (s != nullptr && SelectModel::selectable(s->model().entries()[*entry]) && entry != highlight_) {
      highlight_ = entry;
      requestPaint();
    }
  }
}

void SelectList::onPointerUp(Event&) {
  thumbDrag_ = false;
  filterDragging_ = false;
  requestPaint();
}

void SelectList::onCaptureLost(Event&) {
  thumbDrag_ = false;
  filterDragging_ = false;
}

void SelectList::onPointerWheel(Event& e) {
  if (maxScroll() <= 0.0 || e.wheelY == 0.0) return;
  e.markHandled();
  e.stopPropagation();
  setScroll(scroll_ - e.wheelY * kWheelStep);
}

void SelectList::onClick(Event& e) {
  if (e.button != Button::Left || thumbDrag_) return;
  const double searchH = searchable() ? metrics().search : 0.0;
  if (e.localY < searchH) return;
  core::layout::Rect thumb;
  if (thumbRect(thumb) && core::layout::containsPoint(thumb, e.x, e.y)) return;
  const std::optional<size_t> entry = entryAt(e.localY - searchH + scroll_);
  Select* s = owner();
  if (!entry || s == nullptr || !SelectModel::selectable(s->model().entries()[*entry])) return;
  s->choose(*entry);  // destroys this widget
}

// ---- keyboard -------------------------------------------------------------------------------------------------------

void SelectList::chooseHighlighted() {
  Select* s = owner();
  if (s == nullptr) return;
  if (highlight_) s->choose(*highlight_);  // destroys this widget
  else s->close();
}

void SelectList::typeAhead(char32_t codePoint) {
  Select* s = owner();
  if (s == nullptr || codePoint < 0x20) return;
  if (ui().now() - typeAtMs_ > kTypeAheadMs) typeBuffer_.clear();
  typeAtMs_ = ui().now();
  text::appendUtf8(typeBuffer_, codePoint);
  if (const std::optional<size_t> found = s->model().typeAhead(typeBuffer_, highlight_)) moveHighlight(found);
}

void SelectList::onKeyDown(Event& e) {
  Select* s = owner();
  if (s == nullptr) return;
  const SelectModel& model = s->model();
  const bool search = s->searchable();
  const int pageRows = std::max(1, static_cast<int>(viewportHeight() / std::max(1.0, metrics().item)));
  switch (e.key) {
    case Key::Down: e.markHandled(); moveHighlight(model.step(highlight_, 1)); return;
    case Key::Up: e.markHandled(); moveHighlight(model.step(highlight_, -1)); return;
    case Key::PageDown: e.markHandled(); moveHighlight(model.page(highlight_, pageRows)); return;
    case Key::PageUp: e.markHandled(); moveHighlight(model.page(highlight_, -pageRows)); return;
    case Key::Enter: e.markHandled(); chooseHighlighted(); return;
    case Key::Tab:
      e.markHandled();
      s->close();
      return;
    case Key::Escape:
      if (search && !filter_.text().empty()) {
        e.markHandled();
        filter_.setText("");
        applyFilterFromEditor();
      }
      return;  // otherwise unused: the overlay layer closes the list
    default: break;
  }
  if (search) {
    const LineEdit edit = filter_.handleKeyDown(e);
    if (edit.handled) e.markHandled();
    if (edit.textChanged) applyFilterFromEditor();
    if (edit.caretMoved) {
      filter_.noteActivity(ui().now());
      requestPaint();
    }
    return;
  }
  if (e.key == Key::Home) {
    e.markHandled();
    moveHighlight(model.first());
  } else if (e.key == Key::End) {
    e.markHandled();
    moveHighlight(model.last());
  } else if (e.key == Key::Space) {
    e.markHandled();
    if (!typeBuffer_.empty() && ui().now() - typeAtMs_ <= kTypeAheadMs) typeAhead(U' ');
    else chooseHighlighted();
  } else {
    const auto v = static_cast<uint16_t>(e.key);
    if ((v >= 'A' && v <= 'Z') || (v >= '0' && v <= '9')) e.markHandled();  // the character arrives as text input
  }
}

void SelectList::onTextInput(Event& e) {
  Select* s = owner();
  if (s == nullptr) return;
  if ((e.modifiers & (Mod::kCtrl | Mod::kMeta)) != 0 && (e.modifiers & Mod::kAlt) == 0) return;
  e.markHandled();
  if (s->searchable()) {
    const LineEdit edit = filter_.handleChar(e.codePoint, e.modifiers);
    if (edit.textChanged) applyFilterFromEditor();
    filter_.noteActivity(ui().now());
    requestPaint();
  } else {
    typeAhead(e.codePoint);
  }
}

void SelectList::onFocusIn(Event&) {
  filter_.noteActivity(ui().now());
  if (searchable() && ui().animationsActive()) ui().invalidator().requestAnimation(id());
}

}  // namespace r1ui::widgets
