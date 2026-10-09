// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of MenuEditor.h, part 2: the pointer and keyboard interaction, the actions they
//   trigger (hide, remove, move by key), the in-place rename and the command picker / context menu.
// Invariants: an armed or running drag always ends through the hub (release, Escape or capture loss);
//   the rename field exists at most once and is destroyed from its own finish callback; handlers never
//   touch members after a call that can rebuild the editor and are written so that a rebuild in between
//   is harmless.
// Callers: CustomizableMenuBar, the gallery, tests.
#include <algorithm>

#include "CustomizeCommon.h"
#include "MenuEditorMetrics.h"
#include "r1ui/widgets/customize/CommandPicker.h"
#include "r1ui/widgets/customize/MenuEditor.h"
#include "r1ui/widgets/customize/RenameField.h"

namespace r1ui::widgets {

namespace cz = commands::customize;
namespace layout = core::layout;
using core::events::Button;
using core::events::Key;
namespace Mod = core::events::Mod;
using cust::kEyeWidth;
using cust::kLabelLeft;

namespace {

constexpr Key kKeyF2 = static_cast<Key>(113);

bool takesLabel(cz::Kind kind) {
  return kind == cz::Kind::Menu || kind == cz::Kind::Section || kind == cz::Kind::Heading || kind == cz::Kind::Submenu || kind == cz::Kind::Command;
}

}  // namespace

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
  if (renaming()) {
    ui().clearFocus();  // a press elsewhere ends the in-place rename (a changed text is committed)
    return;
  }
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
