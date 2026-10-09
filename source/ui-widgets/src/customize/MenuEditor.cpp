// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of MenuEditor.h: building the rows from the model's edit view, hit testing,
//   painting, the pointer/keyboard interaction, the drop position and legality, the in-place rename
//   and the context menu.
// Invariants: rows_ and titles_ mirror Customization::editView() as of the last rebuild() (called on
//   every model or edit-mode notification), no pointer into the model is kept; an armed or running
//   drag always ends through the hub (release, Escape or capture loss); the rename field exists at most
//   once and is destroyed from its own finish callback; handlers never touch members after a call that
//   can rebuild the editor and are written so that a rebuild in between is harmless.
// Callers: CustomizableMenuBar, the gallery, tests.
#include "r1ui/widgets/customize/MenuEditor.h"

#include <algorithm>
#include <cmath>

#include "CustomizeCommon.h"
#include "r1ui/widgets/customize/CommandPicker.h"
#include "r1ui/widgets/customize/RenameField.h"

namespace r1ui::widgets {

namespace cz = commands::customize;
namespace layout = core::layout;
using core::events::Button;
using core::events::Key;
namespace Mod = core::events::Mod;

namespace {

constexpr double kGripLeft = 4.0;
constexpr double kGripWidth = 22.0;
constexpr double kEyeWidth = 30.0;
constexpr double kLabelLeft = 30.0;
constexpr Key kKeyF2 = static_cast<Key>(113);

bool takesLabel(cz::Kind kind) {
  return kind == cz::Kind::Menu || kind == cz::Kind::Section || kind == cz::Kind::Heading || kind == cz::Kind::Submenu || kind == cz::Kind::Command;
}

// Runs the operation a drop stands for on `m` (a real call or a preview).
cz::EditResult runDrop(cz::Customization& m, const DragPayload& payload, const cz::Placement& at) {
  if (payload.kind == DragPayload::Kind::Command) return m.addCommand(at.parent, payload.commandId, at.anchor, at.side);
  return m.move(payload.nodeId, at);
}

}  // namespace

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

// ---- pointer ------------------------------------------------------------------------------------

std::string MenuEditor::lockTip(const std::string& nodeId) const { return controller_.model().lockReason(nodeId); }

void MenuEditor::updateHover(double x, double y) {
  const Hit hit = hitAt(x, y);
  std::string tip;
  if (hit.part == Part::Title || hit.part == Part::TitleEye) {
    const TitleTab& t = titles_[static_cast<size_t>(hit.index)];
    tip = t.locked ? lockTip(t.id) : (hit.part == Part::TitleEye ? (t.visible ? "Hide this menu" : "Show this menu") : "");
  } else if (hit.part == Part::Handle) {
    tip = "Drag to move";
  } else if (hit.part != Part::None && hit.part != Part::NewMenu && hit.index >= 0) {
    const Row& r = rows_[static_cast<size_t>(hit.index)];
    if (r.locked) {
      tip = lockTip(r.id);
    } else if (hit.part == Part::Eye) {
      tip = r.visible ? "Hide" : "Show";
    } else if (r.missing) {
      tip = "This command is not available";
    }
  } else if (hit.part == Part::NewMenu) {
    tip = "Create a menu of your own";
  }
  if (hit.part != hover_.part || hit.index != hover_.index || tip != tip_) {
    hover_ = hit;
    tip_ = tip;
    requestPaint();
  }
}

void MenuEditor::onPointerMove(Event& e) {
  if (dragging_) {
    controller_.drag().move(e.x, e.y);
    e.markHandled();
    return;
  }
  updateHover(e.x, e.y);
}

void MenuEditor::onPointerLeave(Event&) {
  if (dragging_) return;
  hover_ = {};
  tip_.clear();
  requestPaint();
}

void MenuEditor::onPointerDown(Event& e) {
  if (renaming()) return;
  const Hit hit = hitAt(e.x, e.y);
  if (e.button == Button::Right) {
    if (hit.part == Part::Title || hit.part == Part::TitleEye) {
      e.markHandled();
      openContextMenu(titles_[static_cast<size_t>(hit.index)].id, e.x, e.y);
    } else if (hit.index >= 0 && hit.part != Part::None) {
      e.markHandled();
      cursor_ = rows_[static_cast<size_t>(hit.index)].id;
      openContextMenu(cursor_, e.x, e.y);
    }
    return;
  }
  if (e.button != Button::Left) return;
  ui().router().focus(id(), core::events::FocusReason::Pointer);
  e.markHandled();
  pressed_ = hit;
  armedId_.clear();
  switch (hit.part) {
    case Part::Title:
      setCurrentMenu(titles_[static_cast<size_t>(hit.index)].id);
      if (!titles_[static_cast<size_t>(hit.index)].locked) {
        armedId_ = titles_[static_cast<size_t>(hit.index)].id;
        ui().router().capturePointer(id());
      }
      break;
    case Part::Handle:
      cursor_ = rows_[static_cast<size_t>(hit.index)].id;
      armedId_ = cursor_;
      ui().router().capturePointer(id());
      requestPaint();
      break;
    case Part::Body:
    case Part::Eye:
      cursor_ = rows_[static_cast<size_t>(hit.index)].id;
      requestPaint();
      break;
    default: break;
  }
}

void MenuEditor::onDragStart(Event& e) {
  if (armedId_.empty() || dragging_) return;
  DragPayload payload;
  payload.kind = DragPayload::Kind::Node;
  payload.nodeId = armedId_;
  const cz::Node* node = controller_.model().find(armedId_);
  payload.text = node != nullptr ? controller_.model().shownLabel(*node) : armedId_;
  dragging_ = controller_.drag().begin(std::move(payload), e.x, e.y);
  e.markHandled();
}

void MenuEditor::onPointerUp(Event& e) {
  if (e.button != Button::Left) return;
  const bool wasDragging = dragging_;
  dragging_ = false;
  armedId_.clear();
  if (wasDragging) controller_.drag().end(e.x, e.y);
}

void MenuEditor::onCaptureLost(Event&) { cancelDrag(); }

void MenuEditor::cancelDrag() {
  if (dragging_) controller_.drag().cancel();
  dragging_ = false;
  armedId_.clear();
}

void MenuEditor::onClick(Event& e) {
  if (e.button != Button::Left || renaming()) return;
  const Hit hit = hitAt(e.x, e.y);
  if (hit.part != pressed_.part || hit.index != pressed_.index) return;
  switch (hit.part) {
    case Part::TitleEye:
      e.markHandled();
      toggleHidden(titles_[static_cast<size_t>(hit.index)].id);
      break;
    case Part::Eye:
      e.markHandled();
      toggleHidden(rows_[static_cast<size_t>(hit.index)].id);
      break;
    case Part::NewMenu:
      e.markHandled();
      controller_.createUserMenu();
      break;
    default: break;
  }
}

void MenuEditor::onDoubleClick(Event& e) {
  if (e.button != Button::Left || renaming()) return;
  const Hit hit = hitAt(e.x, e.y);
  if (hit.part == Part::Title && hit.index >= 0) {
    e.markHandled();
    beginRename(titles_[static_cast<size_t>(hit.index)].id);
  } else if ((hit.part == Part::Body || hit.part == Part::Handle) && hit.index >= 0) {
    e.markHandled();
    beginRename(rows_[static_cast<size_t>(hit.index)].id);
  }
}

// ---- actions ------------------------------------------------------------------------------------

void MenuEditor::toggleHidden(const std::string& nodeId) {
  const cz::Node* node = controller_.model().find(nodeId);
  if (node == nullptr) return;
  controller_.noteResult(controller_.model().setHidden(nodeId, node->visible));
}

void MenuEditor::removeOrHide(const std::string& nodeId) {
  const cz::Node* node = controller_.model().find(nodeId);
  if (node == nullptr) return;
  if (node->user) {
    controller_.noteResult(node->kind == cz::Kind::Menu ? controller_.model().deleteUserMenu(nodeId) : controller_.model().removeUserEntry(nodeId));
  } else {
    controller_.noteResult(controller_.model().setHidden(nodeId, true));
  }
}

void MenuEditor::moveByKey(const std::string& nodeId, int direction) {
  const int index = rowIndex(nodeId);
  if (index < 0) return;
  const Row& me = rows_[static_cast<size_t>(index)];
  const bool section = me.kind == cz::Kind::Section;
  // Siblings: rows with the same parent and kind class, in display order.
  std::vector<const Row*> siblings;
  for (const Row& r : rows_) {
    if (r.parent == me.parent && (r.kind == cz::Kind::Section) == section) siblings.push_back(&r);
  }
  const auto self = std::find_if(siblings.begin(), siblings.end(), [&](const Row* r) { return r->id == nodeId; });
  if (self == siblings.end()) return;
  cz::Placement to;
  if (direction < 0) {
    if (self != siblings.begin()) {
      to = {me.parent, (*(self - 1))->id, cz::Side::Before};
    } else if (!section) {
      // The first entry of a section goes to the end of the previous section of the same menu.
      const Row* previous = nullptr;
      for (const Row& r : rows_) {
        if (r.id == me.parent) break;
        if (r.kind == cz::Kind::Section && r.parent == rows_[static_cast<size_t>(rowIndex(me.parent))].parent) previous = &r;
      }
      if (previous == nullptr) return;
      to = {previous->id, "", cz::Side::End};
    } else {
      return;
    }
  } else {
    if (self + 1 != siblings.end()) {
      to = {me.parent, (*(self + 1))->id, cz::Side::After};
    } else if (!section) {
      const Row* next = nullptr;
      bool passed = false;
      for (const Row& r : rows_) {
        if (r.id == me.parent) {
          passed = true;
          continue;
        }
        if (passed && r.kind == cz::Kind::Section && r.parent == rows_[static_cast<size_t>(rowIndex(me.parent))].parent) {
          next = &r;
          break;
        }
      }
      if (next == nullptr) return;
      to = {next->id, "", cz::Side::Start};
    } else {
      return;
    }
  }
  const cz::EditResult r = controller_.model().move(nodeId, to);
  controller_.noteResult(r);
  if (r.ok) cursor_ = nodeId;
}

void MenuEditor::onFocusIn(Event&) { requestPaint(); }
void MenuEditor::onFocusOut(Event&) { requestPaint(); }

void MenuEditor::onKeyDown(Event& e) {
  if (e.key == Key::Escape && dragging_) {
    cancelDrag();
    ui().router().cancelPointerInteraction();
    e.markHandled();
    return;
  }
  if (renaming()) return;
  const bool alt = (e.modifiers & Mod::kAlt) != 0;
  if (e.modifiers & (Mod::kCtrl | Mod::kMeta)) return;
  const int index = rowIndex(cursor_);
  const auto moveCursor = [&](int to) {
    if (rows_.empty()) return;
    to = std::clamp(to, 0, static_cast<int>(rows_.size()) - 1);
    cursor_ = rows_[static_cast<size_t>(to)].id;
    requestPaint();
  };
  if (e.key == kKeyF2) {
    if (!cursor_.empty()) beginRename(cursor_);
    e.markHandled();
    return;
  }
  switch (e.key) {
    case Key::Up:
      if (alt) {
        moveByKey(cursor_, -1);
      } else {
        moveCursor(index < 0 ? static_cast<int>(rows_.size()) - 1 : index - 1);
      }
      break;
    case Key::Down:
      if (alt) {
        moveByKey(cursor_, 1);
      } else {
        moveCursor(index < 0 ? 0 : index + 1);
      }
      break;
    case Key::Home: moveCursor(0); break;
    case Key::End: moveCursor(static_cast<int>(rows_.size()) - 1); break;
    case Key::Left:
    case Key::Right: {
      const auto it = std::find_if(titles_.begin(), titles_.end(), [&](const TitleTab& t) { return t.id == current_; });
      if (it == titles_.end()) break;
      const std::ptrdiff_t at = (it - titles_.begin()) + (e.key == Key::Right ? 1 : -1);
      if (at >= 0 && at < static_cast<std::ptrdiff_t>(titles_.size())) setCurrentMenu(titles_[static_cast<size_t>(at)].id);
      break;
    }
    case Key::Space:
      if (!cursor_.empty()) toggleHidden(cursor_);
      break;
    case Key::Enter:
      if (!cursor_.empty()) beginRename(cursor_);
      break;
    case Key::Delete:
      if (!cursor_.empty()) removeOrHide(cursor_);
      break;
    case Key::Insert: addCommandAtCursor(); break;
    default: return;
  }
  e.markHandled();
}

// ---- drag and drop ------------------------------------------------------------------------------

cz::Kind MenuEditor::payloadKind(const DragPayload& payload) const {
  if (payload.kind == DragPayload::Kind::Command) return cz::Kind::Command;
  const cz::Node* node = controller_.model().find(payload.nodeId);
  return node != nullptr ? node->kind : cz::Kind::Command;
}

MenuEditor::Candidate MenuEditor::locate(const DragPayload& payload, double x, double y) const {
  Candidate c;
  const layout::Rect self = ui().absRect(id());
  const double lx = x - self.x, ly = y - self.y;
  if (lx < 0.0 || ly < 0.0 || lx >= self.w || ly >= self.h) return c;
  const cz::Kind kind = payloadKind(payload);
  const cz::LayoutSet& set = controller_.model().editView().layout;
  const double lineLeft = 6.0, lineWidth = std::max(0.0, self.w - 12.0);

  if (ly < kTitleHeight) {
    for (size_t i = 0; i < titles_.size(); ++i) {
      const TitleTab& t = titles_[i];
      if (lx < t.x || lx >= t.x + t.w) continue;
      if (kind == cz::Kind::Menu) {
        const bool before = lx < t.x + t.w * 0.5;
        c.placement = {set.menuBar.id, t.id, before ? cz::Side::Before : cz::Side::After};
        c.line = local((before ? t.x : t.x + t.w) - 1.0, 4.0, 2.0, kTitleHeight - 8.0);
      } else if (kind == cz::Kind::Section) {
        c.placement = {t.id, "", cz::Side::End};
        c.title = static_cast<int>(i);
      } else {
        const cz::Node* menu = controller_.model().find(t.id);
        if (menu == nullptr) return c;
        if (!menu->children.empty()) {
          c.placement = {menu->children.back().id, "", cz::Side::End};
        } else {
          c.placement = {menu->id, "", cz::Side::End};
        }
        c.title = static_cast<int>(i);
      }
      c.valid = true;
      return c;
    }
    return c;
  }
  if (kind == cz::Kind::Menu) return c;
  if (current_.empty()) return c;

  // Top-level sections of the current menu, with the y range each one covers.
  struct Range {
    std::string id;
    double top, bottom;
  };
  std::vector<Range> sections;
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].kind == cz::Kind::Section && rows_[i].depth == 0) sections.push_back({rows_[i].id, rows_[i].y, rows_.back().y + rows_.back().h});
  }
  for (size_t i = 0; i + 1 < sections.size(); ++i) sections[i].bottom = sections[i + 1].top;
  const double endY = rows_.empty() ? rowsTop() : rows_.back().y + rows_.back().h;

  if (kind == cz::Kind::Section) {
    if (sections.empty()) {
      c.placement = {current_, "", cz::Side::End};
      c.line = local(lineLeft, rowsTop() - 1.0, lineWidth, 2.0);
      c.valid = true;
      return c;
    }
    for (const Range& r : sections) {
      if (ly < r.top || ly >= r.bottom) continue;
      const bool before = ly < r.top + (r.bottom - r.top) * 0.5;
      c.placement = {current_, r.id, before ? cz::Side::Before : cz::Side::After};
      c.line = local(lineLeft, (before ? r.top : r.bottom) - 1.0, lineWidth, 2.0);
      c.valid = true;
      return c;
    }
    // Below the last row: the end of the menu.
    c.placement = {current_, sections.back().id, cz::Side::After};
    c.line = local(lineLeft, endY - 1.0, lineWidth, 2.0);
    c.valid = true;
    return c;
  }

  // Entry-level payloads.
  if (rows_.empty()) {
    c.placement = {current_, "", cz::Side::End};  // a command added to a menu without sections gets one
    c.line = local(lineLeft, rowsTop() - 1.0, lineWidth, 2.0);
    c.valid = true;
    return c;
  }
  for (const Row& r : rows_) {
    if (ly < r.y || ly >= r.y + r.h) continue;
    const double indent = kLabelLeft + (r.depth + (r.kind == cz::Kind::Section ? 1 : 0)) * kIndent;
    if (r.kind == cz::Kind::Section) {
      c.placement = {r.id, "", cz::Side::Start};
      c.line = local(indent, r.y + r.h - 1.0, std::max(0.0, self.w - indent - 6.0), 2.0);
    } else {
      const bool before = ly < r.y + r.h * 0.5;
      c.placement = {r.parent, r.id, before ? cz::Side::Before : cz::Side::After};
      c.line = local(indent - 8.0, (before ? r.y : r.y + r.h) - 1.0, std::max(0.0, self.w - indent - 6.0 + 8.0), 2.0);
    }
    c.valid = true;
    return c;
  }
  // Below the last row: the end of the last top-level section.
  if (sections.empty()) return c;
  c.placement = {sections.back().id, "", cz::Side::End};
  c.line = local(kLabelLeft + kIndent - 8.0, endY - 1.0, std::max(0.0, self.w - kLabelLeft - kIndent - 6.0), 2.0);
  c.valid = true;
  return c;
}

bool MenuEditor::dragOver(const DragPayload& payload, double x, double y) {
  indicator_ = {};
  const Candidate c = locate(payload, x, y);
  if (!c.valid) {
    requestPaint();
    return false;
  }
  const cz::EditResult result = controller_.model().preview([&](cz::Customization& m) { return runDrop(m, payload, c.placement); });
  if (!result.ok) {
    if (result.error == cz::EditError::Locked) controller_.noteResult(result);
    requestPaint();
    return false;
  }
  indicator_.active = true;
  indicator_.line = c.line;
  indicator_.placement = c.placement;
  indicator_.title = c.title;
  requestPaint();
  return true;
}

void MenuEditor::dragLeave() {
  if (!indicator_.active) return;
  indicator_ = {};
  requestPaint();
}

bool MenuEditor::dragDrop(const DragPayload& payload, double x, double y) {
  indicator_ = {};
  const Candidate c = locate(payload, x, y);
  if (!c.valid) return false;
  const cz::EditResult result = runDrop(controller_.model(), payload, c.placement);
  controller_.noteResult(result);
  return result.ok;
}

// ---- rename -------------------------------------------------------------------------------------

bool MenuEditor::beginRename(const std::string& nodeId) {
  endRename();
  const cz::Node* node = controller_.model().find(nodeId);
  if (node == nullptr || !takesLabel(node->kind)) return false;
  if (node->locked) {
    cz::EditResult refused;
    refused.error = cz::EditError::Locked;
    refused.reason = controller_.model().lockReason(nodeId);
    controller_.noteResult(refused);
    return false;
  }
  layout::RectD box;
  const auto tab = std::find_if(titles_.begin(), titles_.end(), [&](const TitleTab& t) { return t.id == nodeId; });
  if (tab != titles_.end()) {
    box = {tab->x + 4.0, 3.0, std::max(120.0, tab->w), kTitleHeight - 6.0};
  } else {
    const int index = rowIndex(nodeId);
    if (index < 0) return false;
    const Row& r = rows_[static_cast<size_t>(index)];
    const double left = kLabelLeft + r.depth * kIndent + (r.kind == cz::Kind::Command ? 24.0 : 0.0);
    box = {left, r.y + 2.0, std::max(120.0, ui().absRect(id()).w - left - kEyeWidth), r.h - 4.0};
  }
  const std::string original = controller_.model().shownLabel(*node);
  RenameField& field = ui().create<RenameField>(id(), original);
  field.style().position = layout::Position::Absolute;
  field.style().inset[layout::kLeft] = layout::Length::px(box.x);
  field.style().inset[layout::kTop] = layout::Length::px(box.y);
  field.style().width = layout::Length::px(box.w);
  field.style().height = layout::Length::px(std::max(22.0, box.h));
  rename_ = field.id();
  renameTarget_ = nodeId;
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  field.setOnFinish([context, self, nodeId](bool commit, const std::string& text) {
    MenuEditor* e = context->objectAs<MenuEditor>(self);
    if (e == nullptr) return;
    e->endRename();
    if (commit) e->controller_.noteResult(e->controller_.model().renameLabel(nodeId, text));
    e->requestPaint();
  });
  requestLayout();
  ui().focusWidget(field.id(), core::events::FocusReason::Keyboard);
  return true;
}

void MenuEditor::endRename() {
  if (!rename_.valid()) return;
  const core::tree::WidgetId field = rename_;
  rename_ = {};
  renameTarget_.clear();
  ui().destroy(field);
  if (ui().alive(id())) ui().focusWidget(id(), core::events::FocusReason::Program);
}

// ---- add commands, context menu -----------------------------------------------------------------

namespace {

struct Insertion {
  std::string parent, anchor;
  cz::Side side = cz::Side::End;
};

}  // namespace

void MenuEditor::addCommandAtCursor() {
  CommandPickerOptions options;
  options.title = "Add a command to the menu";
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  options.onChosen = [context, self](const std::string& commandId) {
    MenuEditor* e = context->objectAs<MenuEditor>(self);
    if (e == nullptr) return;
    Insertion at{e->current_, "", cz::Side::End};
    const int index = e->rowIndex(e->cursor_);
    if (index >= 0) {
      const Row& r = e->rows_[static_cast<size_t>(index)];
      if (r.kind == cz::Kind::Section) {
        at = {r.id, "", cz::Side::Start};
      } else {
        at = {r.parent, r.id, cz::Side::After};
      }
    }
    e->controller_.noteResult(e->controller_.model().addCommand(at.parent, commandId, at.anchor, at.side));
  };
  openCommandPicker(controller_, std::move(options));
}

bool MenuEditor::openContextMenu(const std::string& nodeId, double x, double y) {
  const cz::Node* node = controller_.model().find(nodeId);
  if (node == nullptr) return false;
  const bool locked = node->locked;
  const bool title = node->kind == cz::Kind::Menu;
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  const auto act = [context, self](auto fn) {
    return [context, self, fn](const MenuItemSpec&) {
      if (MenuEditor* e = context->objectAs<MenuEditor>(self)) fn(*e);
    };
  };
  MenuSpec spec;
  const auto add = [&](const char* tag, const std::string& label, bool enabled, std::function<void(MenuEditor&)> fn) {
    MenuItemSpec item = menuAction(std::string("customize:") + tag, label);
    item.enabled = enabled;
    item.onActivate = act(std::move(fn));
    spec.items.push_back(std::move(item));
  };
  add("visibility", node->visible ? "Hide" : "Show", !locked, [nodeId](MenuEditor& e) { e.toggleHidden(nodeId); });
  add("rename", "Rename...", !locked && takesLabel(node->kind), [nodeId](MenuEditor& e) { e.beginRename(nodeId); });
  spec.items.push_back(menuSeparator());
  const auto insertionFor = [nodeId](MenuEditor& e) {
    Insertion at{e.current_, "", cz::Side::End};
    const int index = e.rowIndex(nodeId);
    if (index >= 0) {
      const Row& r = e.rows_[static_cast<size_t>(index)];
      at = r.kind == cz::Kind::Section ? Insertion{r.id, "", cz::Side::Start} : Insertion{r.parent, r.id, cz::Side::After};
    } else {
      e.cursor_.clear();
    }
    return at;
  };
  add("addCommand", "Add command...", !locked || title, [nodeId](MenuEditor& e) {
    if (e.rowIndex(nodeId) >= 0) e.cursor_ = nodeId;
    e.addCommandAtCursor();
  });
  add("addSeparator", "Add separator", !locked && !title, [insertionFor](MenuEditor& e) {
    const Insertion at = insertionFor(e);
    e.controller_.noteResult(e.controller_.model().addSeparator(at.parent, at.anchor, at.side));
  });
  add("addHeading", "Add heading", !locked && !title, [insertionFor](MenuEditor& e) {
    const Insertion at = insertionFor(e);
    const cz::EditResult r = e.controller_.model().addHeading(at.parent, "New heading", at.anchor, at.side);
    e.controller_.noteResult(r);
    if (r.ok) e.beginRename(r.id);
  });
  add("addSubmenu", "Add sub-menu", !locked && !title, [insertionFor](MenuEditor& e) {
    const Insertion at = insertionFor(e);
    const cz::EditResult r = e.controller_.model().addSubmenu(at.parent, "New sub-menu", at.anchor, at.side);
    e.controller_.noteResult(r);
    if (r.ok) e.beginRename(r.id);
  });
  add("addSection", "Add section", !locked, [nodeId, title](MenuEditor& e) {
    const std::string menu = title ? nodeId : e.current_;
    e.controller_.noteResult(e.controller_.model().addSection(menu, ""));
  });
  spec.items.push_back(menuSeparator());
  if (node->user) {
    add("remove", title ? "Delete menu" : "Remove", !locked, [nodeId](MenuEditor& e) { e.removeOrHide(nodeId); });
  }
  const std::string menuId = title ? nodeId : current_;
  add("reset", "Reset this menu", !controller_.model().isLocked(menuId), [menuId](MenuEditor& e) { e.controller_.noteResult(e.controller_.model().resetMenu(menuId)); });
  spec.onCommand = [](const MenuItemSpec&) {};
  return contextMenu_->openContextMenu(std::move(spec), x, y);
}

}  // namespace r1ui::widgets
