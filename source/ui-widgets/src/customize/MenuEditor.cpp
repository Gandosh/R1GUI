// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of MenuEditor.h, part 1: the lifecycle, the mirror of the model's edit view (rows
//   and title tabs), the geometry and hit testing, and the painting. Input handling is in
//   MenuEditorInput.cpp, the drop logic in MenuEditorDrop.cpp.
// Invariants: rows_ and titles_ mirror Customization::editView() as of the last rebuild() (called on
//   every model or edit-mode notification), no pointer into the model is kept; the editor's height and
//   minimum width follow its content so layout never needs a measure callback.
// Callers: CustomizableMenuBar, the gallery, tests.
#include "r1ui/widgets/customize/MenuEditor.h"

#include <algorithm>
#include <cmath>

#include "CustomizeCommon.h"
#include "MenuEditorMetrics.h"

namespace r1ui::widgets {

namespace cz = commands::customize;
namespace layout = core::layout;
using cust::kEyeWidth;
using cust::kGripLeft;
using cust::kGripWidth;
using cust::kLabelLeft;

MenuEditor::~MenuEditor() = default;

// ---- lifecycle ----------------------------------------------------------------------------------

void MenuEditor::onAttached() {
  setFocusable(true);
  style().flexShrink = 0.0;
  style().alignSelf = layout::Align::Stretch;
  contextMenu_ = std::make_unique<MenuController>(ui());
  controller_.drag().addTarget(id(), this);
  registered_ = true;
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  listener_ = controller_.subscribe([context, self] {
    MenuEditor* e = context->objectAs<MenuEditor>(self);
    if (e == nullptr) return;
    e->rebuild();
    const std::string pending = e->controller_.takePendingRename();
    if (!pending.empty()) {
      e->setCurrentMenu(pending);
      e->beginRename(pending);
    }
  });
  rebuild();
}

void MenuEditor::onDetached() {
  cancelDrag();  // a source destroyed in the middle of a drag must not leave the hub (and its ghost) running
  controller_.unsubscribe(listener_);
  if (registered_) controller_.drag().removeTarget(id());
  registered_ = false;
  if (contextMenu_) contextMenu_->close();
}

// ---- model mirror -------------------------------------------------------------------------------

void MenuEditor::appendEntry(const cz::Node& entry, const std::string& parent, int depth) {
  Row row;
  row.id = entry.id;
  row.parent = parent;
  row.kind = entry.kind;
  row.depth = depth;
  row.visible = entry.visible;
  row.locked = entry.locked;
  row.user = entry.user;
  row.missing = entry.missing;
  row.label = controller_.model().shownLabel(entry);
  switch (entry.kind) {
    case cz::Kind::Command:
      row.shortcut = controller_.services().keymap.displayText(entry.commandId);
      row.icon = cust::commandIconOf(controller_.services(), entry.commandId);
      row.h = kEntryHeight;
      break;
    case cz::Kind::Separator: row.h = kDividerHeight; break;
    case cz::Kind::Heading: row.h = kHeadingHeight; break;
    case cz::Kind::Submenu:
      row.h = kEntryHeight;
      row.hasSubmenu = true;
      break;
    default: row.h = kEntryHeight; break;
  }
  rows_.push_back(row);
  if (entry.kind == cz::Kind::Submenu) {
    for (const cz::Node& section : entry.children) appendSection(section, entry.id, depth + 1);
  }
}

void MenuEditor::rebuild() {
  const cz::LayoutSet& set = controller_.model().editView().layout;
  const theme::TextStyle body = ui().services().resolve("label.body", 0).text;
  titles_.clear();
  rows_.clear();
  double x = 8.0;
  for (const cz::Node& menu : set.menuBar.menus) {
    TitleTab t;
    t.id = menu.id;
    t.label = controller_.model().shownLabel(menu);
    t.visible = menu.visible;
    t.locked = menu.locked;
    t.user = menu.user;
    t.x = x;
    t.w = cust::textWidth(ui(), t.label, body) + 24.0 + (t.locked ? 18.0 : 0.0) + 24.0;
    x += t.w + 2.0;
    titles_.push_back(std::move(t));
  }
  const bool canAdd = !set.menuBar.locked;
  newMenuX_ = x + 6.0;
  newMenuW_ = canAdd ? cust::textWidth(ui(), "New menu", body) + 38.0 : 0.0;
  if (std::none_of(titles_.begin(), titles_.end(), [&](const TitleTab& t) { return t.id == current_; })) current_ = titles_.empty() ? std::string() : titles_.front().id;
  controller_.setCurrentMenu(current_);
  double y = rowsTop();
  for (const cz::Node& menu : set.menuBar.menus) {
    if (menu.id != current_) continue;
    for (const cz::Node& section : menu.children) appendSection(section, menu.id, 0);
  }
  for (Row& row : rows_) {
    row.y = y;
    y += row.h;
  }
  if (!cursor_.empty() && rowIndex(cursor_) < 0) cursor_.clear();
  const double height = std::max(rowsTop() + 40.0, y + 8.0);
  const double width = std::max(320.0, newMenuX_ + newMenuW_ + 8.0);  // the title strip always fits
  if (style().minWidth.kind != layout::Length::Kind::Px || style().minWidth.value != width) {
    style().minWidth = layout::Length::px(width);
    requestLayout();
  }
  if (style().height.kind != layout::Length::Kind::Px || style().height.value != height) {
    style().height = layout::Length::px(height);
    requestLayout();
  }
  indicator_ = {};
  requestPaint();
}

// A section row; `parent` is the menu or sub-menu that holds the section.
void MenuEditor::appendSection(const cz::Node& section, const std::string& parent, int depth) {
  Row row;
  row.id = section.id;
  row.parent = parent;
  row.kind = cz::Kind::Section;
  row.depth = depth;
  row.visible = section.visible;
  row.locked = section.locked;
  row.user = section.user;
  row.label = controller_.model().shownLabel(section);
  if (row.label == "Section" && section.shownLabel().empty()) row.label.clear();
  row.h = row.label.empty() ? kDividerHeight : kSectionHeight;
  rows_.push_back(row);
  for (const cz::Node& entry : section.children) appendEntry(entry, section.id, depth + 1);
}

// ---- geometry -----------------------------------------------------------------------------------

layout::RectD MenuEditor::local(double x, double y, double w, double h) const {
  const layout::Rect r = ui().absRect(id());
  return {r.x + x, r.y + y, w, h};
}

MenuEditor::TitleView MenuEditor::title(size_t index) const {
  TitleView v;
  if (index >= titles_.size()) return v;
  const TitleTab& t = titles_[index];
  v.id = t.id;
  v.label = t.label;
  v.visible = t.visible;
  v.locked = t.locked;
  v.user = t.user;
  v.selected = t.id == current_;
  v.rect = local(t.x, 2.0, t.w, kTitleHeight - 4.0);
  v.eye = local(t.x + t.w - 24.0, 2.0, 22.0, kTitleHeight - 4.0);
  return v;
}

MenuEditor::RowView MenuEditor::row(size_t index) const {
  RowView v;
  if (index >= rows_.size()) return v;
  const Row& r = rows_[index];
  const layout::Rect self = ui().absRect(id());
  v.id = r.id;
  v.kind = r.kind;
  v.depth = r.depth;
  v.label = r.label;
  v.shortcut = r.shortcut;
  v.visible = r.visible;
  v.locked = r.locked;
  v.user = r.user;
  v.missing = r.missing;
  v.cursor = r.id == cursor_;
  v.rect = local(0.0, r.y, self.w, r.h);
  v.handle = local(kGripLeft, r.y, kGripWidth, r.h);
  v.eye = local(self.w - kEyeWidth, r.y, kEyeWidth - 6.0, r.h);
  v.text = local(kLabelLeft + r.depth * kIndent, r.y, std::max(0.0, self.w - kLabelLeft - r.depth * kIndent - kEyeWidth), r.h);
  return v;
}

int MenuEditor::rowIndex(const std::string& rowId) const {
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].id == rowId) return static_cast<int>(i);
  }
  return -1;
}

layout::RectD MenuEditor::newMenuRect() const { return newMenuW_ > 0.0 ? local(newMenuX_, 3.0, newMenuW_, kTitleHeight - 6.0) : layout::RectD{}; }

bool MenuEditor::setCurrentMenu(const std::string& menuId) {
  if (std::none_of(titles_.begin(), titles_.end(), [&](const TitleTab& t) { return t.id == menuId; })) return false;
  if (menuId == current_) return true;
  endRename();
  current_ = menuId;
  controller_.setCurrentMenu(current_);
  cursor_.clear();
  rebuild();
  return true;
}

void MenuEditor::setCursor(const std::string& rowId) {
  if (rowId == cursor_) return;
  cursor_ = rowId;
  requestPaint();
}

MenuEditor::Hit MenuEditor::hitAt(double x, double y) const {
  const layout::Rect self = ui().absRect(id());
  const double lx = x - self.x, ly = y - self.y;
  Hit hit;
  if (lx < 0.0 || ly < 0.0 || lx >= self.w || ly >= self.h) return hit;
  if (ly < kTitleHeight) {
    for (size_t i = 0; i < titles_.size(); ++i) {
      const TitleTab& t = titles_[i];
      if (lx >= t.x && lx < t.x + t.w) {
        hit.part = lx >= t.x + t.w - 24.0 ? Part::TitleEye : Part::Title;
        hit.index = static_cast<int>(i);
        return hit;
      }
    }
    if (newMenuW_ > 0.0 && lx >= newMenuX_ && lx < newMenuX_ + newMenuW_) hit.part = Part::NewMenu;
    return hit;
  }
  for (size_t i = 0; i < rows_.size(); ++i) {
    const Row& r = rows_[i];
    if (ly < r.y || ly >= r.y + r.h) continue;
    hit.index = static_cast<int>(i);
    if (lx >= self.w - kEyeWidth) {
      hit.part = Part::Eye;
    } else if (lx >= kGripLeft && lx < kGripLeft + kGripWidth && !r.locked) {
      hit.part = Part::Handle;
    } else {
      hit.part = Part::Body;
    }
    return hit;
  }
  return hit;
}

// ---- painting -----------------------------------------------------------------------------------

Cursor MenuEditor::cursor() const {
  switch (hover_.part) {
    case Part::Handle: return Cursor::Move;
    case Part::Title:
    case Part::TitleEye:
    case Part::NewMenu:
    case Part::Eye: return Cursor::Pointer;
    default: return Cursor::Default;
  }
}

void MenuEditor::paint(PaintContext& ctx) {
  const layout::Rect self = ctx.rect();
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(8.0));
  ctx.painter().fillRoundedRect(ctx.box(), radii, ctx.color("panel"));
  ctx.painter().border(ctx.box(), radii, ctx.hairline(), ctx.color("border"));
  const theme::TextStyle body = ctx.style("label.body").text;
  const theme::TextStyle muted = ctx.style("label.muted").text;
  const render::Color mutedColor = ctx.color(muted.color);
  const render::Color textColor = ctx.color(body.color);
  const render::Color danger = ctx.color("danger");
  const render::Color accent = ctx.color("accent");

  // Title strip.
  for (size_t i = 0; i < titles_.size(); ++i) {
    const TitleTab& t = titles_[i];
    const bool selected = t.id == current_;
    const bool hovered = hover_.index == static_cast<int>(i) && (hover_.part == Part::Title || hover_.part == Part::TitleEye);
    if (!t.visible) ctx.painter().pushOpacity(0.45f);
    if (selected || hovered) {
      ctx.painter().fillRoundedRect(ctx.toPhysical(self.x + t.x, self.y + 3.0, t.w, kTitleHeight - 6.0), render::CornerRadii::uniform(ctx.px(6.0)),
                                    selected ? ctx.color("hover") : ctx.color("hover", 0.5));
    }
    double tx = self.x + t.x + 12.0;
    if (t.locked) {
      cust::drawIconSafe(ctx, "lock", 12.0, ctx.toPhysical(tx, self.y, 12.0, kTitleHeight), mutedColor);
      tx += 18.0;
    }
    TextOptions o;
    o.color = selected ? ctx.color("surface") : mutedColor;
    ctx.drawText(t.label, body, ctx.toPhysical(tx, self.y, t.w - 24.0 - (tx - self.x - t.x), kTitleHeight), o);
    cust::drawIconSafe(ctx, t.visible ? "eye" : "eye-off", 14.0, ctx.toPhysical(self.x + t.x + t.w - 24.0, self.y, 22.0, kTitleHeight), mutedColor);
    if (!t.visible) ctx.painter().popOpacity();
  }
  if (newMenuW_ > 0.0) {
    const layout::RectD r = newMenuRect();
    const bool hovered = hover_.part == Part::NewMenu;
    if (hovered) ctx.painter().fillRoundedRect(ctx.toPhysical(r.x, r.y, r.w, r.h), render::CornerRadii::uniform(ctx.px(6.0)), ctx.color("hover", 0.6));
    cust::drawIconSafe(ctx, "plus", 14.0, ctx.toPhysical(r.x + 6.0, r.y, 16.0, r.h), mutedColor);
    TextOptions o;
    o.padLeft = 26.0;
    o.color = mutedColor;
    ctx.drawText("New menu", body, ctx.toPhysical(r.x, r.y, r.w, r.h), o);
  }
  ctx.painter().fillRect(ctx.toPhysical(self.x + 1.0, self.y + kTitleHeight, self.w - 2.0, 1.0), ctx.color("border", 0.7));

  // Rows.
  for (size_t i = 0; i < rows_.size(); ++i) {
    const Row& r = rows_[i];
    const double y = self.y + r.y;
    if (y + r.h < 0.0 || y > ui().viewportHeight()) continue;  // rows outside the window cost nothing (a menu may have thousands)
    const bool hovered = hover_.index == static_cast<int>(i) && hover_.part != Part::None && hover_.part != Part::Title;
    const bool atCursor = r.id == cursor_;
    if (!r.visible) ctx.painter().pushOpacity(0.45f);
    if (atCursor) {
      ctx.painter().fillRoundedRect(ctx.toPhysical(self.x + 4.0, y, self.w - 8.0, r.h), render::CornerRadii::uniform(ctx.px(5.0)), ctx.color("hover"));
    } else if (hovered) {
      ctx.painter().fillRoundedRect(ctx.toPhysical(self.x + 4.0, y, self.w - 8.0, r.h), render::CornerRadii::uniform(ctx.px(5.0)), ctx.color("hover", 0.5));
    }
    if (!r.locked) cust::drawGrip(ctx, {self.x + kGripLeft, y, kGripWidth, r.h}, mutedColor);
    const double left = self.x + kLabelLeft + r.depth * kIndent;
    const double width = std::max(0.0, self.w - kLabelLeft - r.depth * kIndent - kEyeWidth);
    switch (r.kind) {
      case cz::Kind::Section: {
        if (r.label.empty()) {
          // An unnamed section is the divider between two groups of entries.
          const render::Color line = ctx.color("border");
          for (double dx = 0.0; dx < width; dx += 8.0) {
            ctx.painter().fillRect(ctx.toPhysical(left + dx, y + r.h * 0.5, std::min(4.0, width - dx), 1.0), line);
          }
        } else {
          theme::TextStyle s = muted;
          s.weight = 600;
          TextOptions o;
          o.color = mutedColor;
          ctx.drawText(r.label, s, ctx.toPhysical(left, y, width, r.h), o);
        }
        break;
      }
      case cz::Kind::Separator:
        ctx.painter().fillRect(ctx.toPhysical(left, y + r.h * 0.5, width, 1.0), ctx.color("border"));
        break;
      case cz::Kind::Heading: {
        theme::TextStyle s = body;
        s.weight = 600;
        TextOptions o;
        o.color = mutedColor;
        ctx.drawText(r.label, s, ctx.toPhysical(left, y, width, r.h), o);
        break;
      }
      default: {
        if (r.kind == cz::Kind::Command) cust::drawIconSafe(ctx, r.icon, 14.0, ctx.toPhysical(left, y, 16.0, r.h), mutedColor);
        TextOptions o;
        o.padLeft = r.kind == cz::Kind::Command ? 24.0 : 0.0;
        o.padRight = r.shortcut.empty() ? (r.hasSubmenu ? 22.0 : 0.0) : 96.0;
        o.color = r.missing ? danger : textColor;
        ctx.drawText(r.missing ? r.label + " (missing)" : r.label, body, ctx.toPhysical(left, y, width, r.h), o);
        if (!r.shortcut.empty()) {
          TextOptions c;
          c.align = TextAlign::End;
          c.color = mutedColor;
          ctx.drawText(r.shortcut, muted, ctx.toPhysical(left, y, width, r.h), c);
        }
        if (r.hasSubmenu) cust::drawIconSafe(ctx, "chevron-right", 12.0, ctx.toPhysical(left + width - 18.0, y, 16.0, r.h), mutedColor);
        break;
      }
    }
    const layout::RectD eye{self.x + self.w - kEyeWidth, y, kEyeWidth - 6.0, r.h};
    if (r.locked) {
      cust::drawIconSafe(ctx, "lock", 12.0, ctx.toPhysical(eye.x, eye.y, eye.w, eye.h), mutedColor);
    } else {
      cust::drawIconSafe(ctx, r.visible ? "eye" : "eye-off", 14.0, ctx.toPhysical(eye.x, eye.y, eye.w, eye.h), mutedColor);
    }
    if (!r.visible) ctx.painter().popOpacity();
  }
  if (rows_.empty()) {
    TextOptions o;
    o.color = mutedColor;
    o.padLeft = 12.0;
    ctx.drawText("Drag a command here or press Insert", muted, ctx.toPhysical(self.x, self.y + rowsTop(), self.w, 28.0), o);
  }

  // Insertion indicator and drop highlight.
  if (indicator_.active) {
    if (indicator_.title >= 0 && indicator_.title < static_cast<int>(titles_.size())) {
      const TitleTab& t = titles_[static_cast<size_t>(indicator_.title)];
      ctx.painter().border(ctx.toPhysical(self.x + t.x, self.y + 2.0, t.w, kTitleHeight - 4.0), render::CornerRadii::uniform(ctx.px(6.0)), ctx.px(2.0), accent);
    } else {
      const layout::RectD& l = indicator_.line;
      ctx.painter().fillRoundedRect(ctx.toPhysical(l.x, l.y, l.w, l.h), render::CornerRadii::uniform(ctx.px(1.0)), accent);
      if (l.w > l.h) ctx.painter().fillRoundedRect(ctx.toPhysical(l.x - 3.0, l.y - 2.0, 6.0, 6.0), render::CornerRadii::uniform(ctx.px(3.0)), accent);
    }
  }
  if (focusVisible()) ctx.focusRing(radii.topLeft);
}

}  // namespace r1ui::widgets
