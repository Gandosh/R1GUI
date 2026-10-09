// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CommandPalette.h.
// Invariants: only command rows can be selected; the scroll offset is always inside
//   [0, contentHeight - height]; the registry subscription is removed in onDetached; a drag is armed
//   only by a left press on a command row and ends (hub.end / cancel) on release, Escape or capture loss.
// Callers: hosts, the gallery, the command picker, tests.
#include "r1ui/widgets/customize/CommandPalette.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "CustomizeCommon.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace r1ui::widgets {

namespace layout = core::layout;
using core::events::Button;
using core::events::Key;

namespace {

std::string lower(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

std::vector<std::string> words(const std::string& text) {
  std::vector<std::string> out;
  std::string current;
  for (const char c : lower(text)) {
    if (c == ' ' || c == '\t') {
      if (!current.empty()) out.push_back(std::move(current));
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) out.push_back(std::move(current));
  return out;
}

}  // namespace

// ---- the list -----------------------------------------------------------------------------------

void CommandPaletteList::onAttached() {
  setFocusable(true);
  style().flexGrow = 1.0;
  style().flexShrink = 1.0;
  style().minHeight = layout::Length::px(kRowHeight * 3);
  style().overflow = layout::Overflow::Hidden;
}

double CommandPaletteList::contentHeight() const {
  double h = 0.0;
  for (const PaletteRow& r : rows_) h += r.header ? kHeaderHeight : kRowHeight;
  return h;
}

void CommandPaletteList::setScrollOffset(double offset) {
  const double max = std::max(0.0, contentHeight() - ui().absRect(id()).h);
  const double clamped = std::isfinite(offset) ? std::clamp(offset, 0.0, max) : 0.0;
  if (clamped == scroll_) return;
  scroll_ = clamped;
  requestPaint();
}

double CommandPaletteList::rowTop(int index) const {
  double y = 0.0;
  for (int i = 0; i < index && i < static_cast<int>(rows_.size()); ++i) y += rows_[static_cast<size_t>(i)].header ? kHeaderHeight : kRowHeight;
  return y;
}

layout::RectD CommandPaletteList::rowRect(int index) const {
  if (index < 0 || index >= static_cast<int>(rows_.size())) return {};
  const layout::Rect r = ui().absRect(id());
  const double h = rows_[static_cast<size_t>(index)].header ? kHeaderHeight : kRowHeight;
  const double y = r.y + rowTop(index) - scroll_;
  if (y + h <= r.y || y >= r.y + r.h) return {};
  return {static_cast<double>(r.x), y, static_cast<double>(r.w), h};
}

int CommandPaletteList::rowAt(double x, double y) const {
  const layout::Rect r = ui().absRect(id());
  if (!cust::inside(r, x, y)) return -1;
  double top = r.y - scroll_;
  for (size_t i = 0; i < rows_.size(); ++i) {
    const double h = rows_[i].header ? kHeaderHeight : kRowHeight;
    if (y >= top && y < top + h) return static_cast<int>(i);
    top += h;
  }
  return -1;
}

void CommandPaletteList::setRows(std::vector<PaletteRow> rows) {
  std::string keep;
  if (selected_ >= 0 && selected_ < static_cast<int>(rows_.size())) keep = rows_[static_cast<size_t>(selected_)].commandId;
  rows_ = std::move(rows);
  selected_ = -1;
  hover_ = -1;
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (!rows_[i].header && rows_[i].commandId == keep) selected_ = static_cast<int>(i);
  }
  setScrollOffset(scroll_);
  requestPaint();
}

void CommandPaletteList::setSelected(int index) {
  if (index == selected_) return;
  selected_ = index;
  requestPaint();
  if (selected_ >= 0 && selected_ < static_cast<int>(rows_.size()) && onSelect_) {
    auto callback = onSelect_;
    callback(rows_[static_cast<size_t>(selected_)].commandId);
  }
}

bool CommandPaletteList::select(const std::string& commandId) {
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (!rows_[i].header && rows_[i].commandId == commandId) {
      setSelected(static_cast<int>(i));
      scrollIntoView(selected_);
      return true;
    }
  }
  return false;
}

void CommandPaletteList::scrollIntoView(int index) {
  if (index < 0 || index >= static_cast<int>(rows_.size())) return;
  const double top = rowTop(index);
  const double h = rows_[static_cast<size_t>(index)].header ? kHeaderHeight : kRowHeight;
  const double viewport = ui().absRect(id()).h;
  if (top < scroll_) {
    setScrollOffset(index == 1 && rows_[0].header ? 0.0 : top);  // reveal the header above the first command of a category
  } else if (top + h > scroll_ + viewport) {
    setScrollOffset(top + h - viewport);
  }
}

void CommandPaletteList::moveSelection(int delta) {
  if (rows_.empty() || delta == 0) return;
  int i = selected_;
  const int step = delta > 0 ? 1 : -1;
  for (int remaining = std::abs(delta); remaining > 0; --remaining) {
    int j = i;
    do {
      j += step;
    } while (j >= 0 && j < static_cast<int>(rows_.size()) && rows_[static_cast<size_t>(j)].header);
    if (j < 0 || j >= static_cast<int>(rows_.size())) break;
    i = j;
  }
  if (i == -1 && delta > 0) return;
  if (i != selected_) {
    setSelected(i);
    scrollIntoView(i);
  }
}

void CommandPaletteList::chooseSelected() {
  if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size()) || !onChoose_) return;
  auto callback = onChoose_;
  callback(rows_[static_cast<size_t>(selected_)].commandId);
}

std::string_view CommandPaletteList::tooltipText() const {
  if (hover_ >= 0 && hover_ < static_cast<int>(rows_.size()) && !rows_[static_cast<size_t>(hover_)].description.empty()) return rows_[static_cast<size_t>(hover_)].description;
  return {};
}

void CommandPaletteList::paint(PaintContext& ctx) {
  const layout::Rect area = ctx.rect();
  ctx.painter().fillRect(ctx.box(), ctx.color("panel"));
  ctx.painter().pushClip(ctx.box());
  const theme::TextStyle body = ctx.style("label.body").text;
  const theme::TextStyle muted = ctx.style("label.muted").text;
  double y = area.y - scroll_;
  for (size_t i = 0; i < rows_.size(); ++i) {
    const PaletteRow& row = rows_[i];
    const double h = row.header ? kHeaderHeight : kRowHeight;
    if (y + h >= area.y && y <= area.y + area.h) {
      const render::Rect box = ctx.toPhysical(area.x, y, area.w, h);
      if (row.header) {
        TextOptions o;
        o.padLeft = 10.0;
        theme::TextStyle s = muted;
        s.weight = 600;
        ctx.drawText(row.text, s, box, o);
      } else {
        const bool selected = static_cast<int>(i) == selected_;
        const bool hover = static_cast<int>(i) == hover_;
        const render::Color white{1.0f, 1.0f, 1.0f, 1.0f};
        if (selected) {
          ctx.painter().fillRoundedRect(ctx.toPhysical(area.x + 4, y + 1, area.w - 8, h - 2), render::CornerRadii::uniform(ctx.px(6.0)), ctx.color("accent"));
        } else if (hover) {
          ctx.painter().fillRoundedRect(ctx.toPhysical(area.x + 4, y + 1, area.w - 8, h - 2), render::CornerRadii::uniform(ctx.px(6.0)), ctx.color("hover"));
        }
        const render::Color text = selected ? white : ctx.color(body.color);
        cust::drawIconSafe(ctx, row.icon, 16.0, ctx.toPhysical(area.x + 10, y, 16, h), selected ? white : ctx.color("muted"));
        TextOptions label;
        label.padLeft = 34.0;
        label.padRight = 110.0;
        label.color = text;
        ctx.drawText(row.text, body, box, label);
        if (!row.shortcut.empty()) {
          TextOptions chord;
          chord.align = TextAlign::End;
          chord.padRight = 12.0;
          chord.color = selected ? white : ctx.color(muted.color);
          ctx.drawText(row.shortcut, muted, box, chord);
        }
      }
    }
    y += h;
    if (y > area.y + area.h) break;
  }
  ctx.painter().popClip();
}

void CommandPaletteList::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(4.0));
}

void CommandPaletteList::onPointerDown(Event& e) {
  if (e.button != Button::Left) return;
  const int row = rowAt(e.x, e.y);
  ui().router().focus(id(), core::events::FocusReason::Pointer);
  e.markHandled();
  if (row < 0 || rows_[static_cast<size_t>(row)].header) return;
  setSelected(row);
  pressed_ = row;
  armed_ = true;
  ui().router().capturePointer(id());
}

void CommandPaletteList::onDragStart(Event& e) {
  if (!armed_ || dragging_ || pressed_ < 0 || pressed_ >= static_cast<int>(rows_.size())) return;
  const PaletteRow& row = rows_[static_cast<size_t>(pressed_)];
  DragPayload payload;
  payload.kind = DragPayload::Kind::Command;
  payload.commandId = row.commandId;
  payload.text = row.text;
  dragging_ = controller_.drag().begin(std::move(payload), e.x, e.y);
  e.markHandled();
}

void CommandPaletteList::onPointerMove(Event& e) {
  if (dragging_) {
    controller_.drag().move(e.x, e.y);
    e.markHandled();
    return;
  }
  const int row = rowAt(e.x, e.y);
  const int hover = row >= 0 && !rows_[static_cast<size_t>(row)].header ? row : -1;
  if (hover != hover_) {
    hover_ = hover;
    requestPaint();
  }
}

void CommandPaletteList::onPointerUp(Event& e) {
  if (e.button != Button::Left) return;
  const bool wasDragging = dragging_;
  dragging_ = false;
  armed_ = false;
  pressed_ = -1;
  if (wasDragging) controller_.drag().end(e.x, e.y);
}

void CommandPaletteList::onCaptureLost(Event&) {
  if (dragging_) controller_.drag().cancel();
  dragging_ = false;
  armed_ = false;
  pressed_ = -1;
}

void CommandPaletteList::onPointerLeave(Event&) {
  if (hover_ != -1) {
    hover_ = -1;
    requestPaint();
  }
}

void CommandPaletteList::onPointerWheel(Event& e) {
  const double before = scroll_;
  setScrollOffset(scroll_ - e.wheelY * 48.0);
  if (scroll_ != before) {
    e.markHandled();
    e.stopPropagation();
  }
}

void CommandPaletteList::onDoubleClick(Event& e) {
  const int row = rowAt(e.x, e.y);
  if (row < 0 || rows_[static_cast<size_t>(row)].header) return;
  setSelected(row);
  e.markHandled();
  chooseSelected();
}

void CommandPaletteList::onKeyDown(Event& e) {
  if (e.key == Key::Escape && dragging_) {
    dragging_ = false;
    armed_ = false;
    controller_.drag().cancel();
    ui().router().cancelPointerInteraction();
    e.markHandled();
    return;
  }
  const int page = std::max(1, static_cast<int>(ui().absRect(id()).h / kRowHeight) - 1);
  switch (e.key) {
    case Key::Down: moveSelection(1); break;
    case Key::Up: moveSelection(-1); break;
    case Key::PageDown: moveSelection(page); break;
    case Key::PageUp: moveSelection(-page); break;
    case Key::Home: selected_ = -1; moveSelection(1); break;
    case Key::End: selected_ = static_cast<int>(rows_.size()); moveSelection(-1); break;
    case Key::Enter: chooseSelected(); break;
    default: return;
  }
  e.markHandled();
}

// ---- the palette --------------------------------------------------------------------------------

void CommandPalette::onAttached() {
  style().direction = layout::FlexDirection::Column;
  style().gapRow = 6.0;
  TextInput& field = ui().create<TextInput>(id(), TextInputTone::Default, TextInputSize::Md);
  field.setPlaceholder("Search commands");
  field.setClearable(true);
  field.setAccessibleName("Search commands");
  field.style().flexShrink = 0.0;
  search_ = field.id();
  CommandPaletteList& list = ui().create<CommandPaletteList>(id(), controller_);
  list_ = list.id();
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  field.setOnTextChanged([context, self](std::string_view text) {
    if (CommandPalette* p = context->objectAs<CommandPalette>(self)) p->setFilter(std::string(text));
  });
  field.setOnCommitted([context, self](std::string_view) {
    CommandPalette* p = context->objectAs<CommandPalette>(self);
    if (p == nullptr) return;
    p->list().chooseSelected();
  });
  registryListener_ = controller_.services().registry.subscribe([context, self] {
    if (CommandPalette* p = context->objectAs<CommandPalette>(self)) p->rebuild();
  });
  rebuild();
}

void CommandPalette::onDetached() { controller_.services().registry.unsubscribe(registryListener_); }

CommandPaletteList& CommandPalette::list() const { return *ui().objectAs<CommandPaletteList>(list_); }

void CommandPalette::setOnChoose(std::function<void(const std::string&)> callback) {
  onChoose_ = std::move(callback);
  list().setOnChoose(onChoose_);
}

void CommandPalette::focusSearch() { ui().focusWidget(search_, core::events::FocusReason::Keyboard); }

std::string CommandPalette::selectedCommand() const {
  const CommandPaletteList& l = list();
  const int index = l.selectedIndex();
  return index >= 0 && index < static_cast<int>(l.rows().size()) ? l.rows()[static_cast<size_t>(index)].commandId : std::string();
}

void CommandPalette::setFilter(const std::string& text) {
  if (text == filter_) {
    return;
  }
  filter_ = text;
  if (TextInput* field = ui().objectAs<TextInput>(search_)) {
    if (field->text() != text) field->setText(text);
  }
  rebuild();
}

void CommandPalette::rebuild() {
  const CommandServices& services = controller_.services();
  const std::vector<std::string> terms = words(filter_);
  std::vector<PaletteRow> rows;
  for (const std::string& category : services.registry.categories()) {
    std::vector<PaletteRow> inCategory;
    for (const commands::CommandDef* def : services.registry.inCategory(category)) {
      if (def->hiddenFromEditor) continue;
      PaletteRow row;
      row.commandId = def->id;
      row.text = def->label;
      row.icon = def->icon.empty() ? std::string("circle") : def->icon;
      row.shortcut = services.keymap.displayText(def->id);
      row.description = def->description;
      if (!terms.empty()) {
        const std::string haystack = lower(def->label + " " + def->id + " " + def->description + " " + category + " " + row.shortcut);
        const bool all = std::all_of(terms.begin(), terms.end(), [&](const std::string& t) { return haystack.find(t) != std::string::npos; });
        if (!all) continue;
      }
      inCategory.push_back(std::move(row));
    }
    if (inCategory.empty()) continue;
    PaletteRow header;
    header.header = true;
    header.text = category;
    rows.push_back(std::move(header));
    for (PaletteRow& row : inCategory) rows.push_back(std::move(row));
  }
  CommandPaletteList& l = list();
  l.setRows(std::move(rows));
  if (!terms.empty() && l.selectedIndex() < 0) l.moveSelection(1);  // a search selects its first hit
}

void CommandPalette::onKeyDown(Event& e) {
  CommandPaletteList& l = list();
  switch (e.key) {
    case Key::Down: l.moveSelection(1); break;
    case Key::Up: l.moveSelection(-1); break;
    case Key::PageDown: l.moveSelection(8); break;
    case Key::PageUp: l.moveSelection(-8); break;
    default: return;
  }
  e.markHandled();
}

}  // namespace r1ui::widgets
