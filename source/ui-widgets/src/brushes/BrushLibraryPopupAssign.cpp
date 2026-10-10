// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the two small surfaces drawn inside the brush popup: the tile menu (Pick, favourite, Assign
//   letter, Clear letter) and the Assign letter popover (press a letter, see how many brushes share it,
//   Enter assigns, Delete clears, Escape cancels).
// Invariants: at most one of them is open; while one is open it owns the keyboard (the popup's other
//   keys do nothing), Escape closes only the popover or menu and never the popup; the popover never
//   writes to the model until Enter; a refused letter leaves the model unchanged and says why.
// Callers: BrushLibraryPopup (input and paint code).
#include <algorithm>

#include "BrushPopupGeometry.h"
#include "r1ui/commands/brushes/BrushLetters.h"
#include "r1ui/widgets/brushes/BrushLibraryPopup.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace cb = commands::brushes;
namespace bx = brushes;
using core::events::Key;
using core::layout::RectD;

namespace {

enum MenuAction : int { kActionPick = 0, kActionFavourite = 1, kActionAssign = 2, kActionClear = 3 };

std::string upper(char32_t folded) { return cb::displayKey(std::u32string(1, folded), 1); }

}  // namespace

// ---- tile menu ----------------------------------------------------------------------------------

void BrushLibraryPopup::openMenu(uint32_t brush, double x, double y) {
  if (brush >= model_.size()) return;
  assign_.open = false;
  menu_ = {};
  menu_.open = true;
  menu_.brush = brush;
  const cb::BrushInfo& info = model_.brushes()[brush];
  menu_.labels.push_back("Pick " + info.name);
  menu_.actions.push_back(kActionPick);
  menu_.labels.push_back(model_.isFavourite(info.id) ? "Remove from favourites" : "Add to favourites");
  menu_.actions.push_back(kActionFavourite);
  menu_.labels.push_back("Assign letter...");
  menu_.actions.push_back(kActionAssign);
  if (!model_.userLetter(info.id).empty()) {
    menu_.labels.push_back("Clear letter " + model_.userLetter(info.id));
    menu_.actions.push_back(kActionClear);
  }
  const core::layout::Rect self = ui().absRect(id());
  const double height = static_cast<double>(menu_.labels.size()) * bx::kMenuRow + 8.0;
  menu_.x = std::clamp(x - self.x, 4.0, std::max(4.0, widthNow() - bx::kMenuWidth - 4.0));
  menu_.y = std::clamp(y - self.y, 4.0, std::max(4.0, heightNow() - height - 4.0));
  requestPaint();
}

void BrushLibraryPopup::showMenu(uint32_t brush) {
  for (size_t t = 0; t < result_.tiles.size(); ++t) {
    if (result_.tiles[t].brush != brush) continue;
    const RectD tile = tileRect(t);
    if (tile.w > 0.0) openMenu(brush, tile.x + tile.w * 0.5, tile.y + tile.h * 0.5);
    return;
  }
}

RectD BrushLibraryPopup::menuRect() const {
  return abs(menu_.x, menu_.y, bx::kMenuWidth, static_cast<double>(menu_.labels.size()) * bx::kMenuRow + 8.0);
}

void BrushLibraryPopup::closeMenu() {
  menu_ = {};
  requestPaint();
}

void BrushLibraryPopup::menuKey(Event& e) {
  const int count = static_cast<int>(menu_.labels.size());
  switch (e.key) {
    case Key::Escape: closeMenu(); return;
    case Key::Up: menu_.selected = (menu_.selected + count - 1) % std::max(1, count); break;
    case Key::Down: menu_.selected = (menu_.selected + 1) % std::max(1, count); break;
    case Key::Home: menu_.selected = 0; break;
    case Key::End: menu_.selected = std::max(0, count - 1); break;
    case Key::Enter:
      if (!e.repeat) runMenuAction(menu_.selected);
      return;
    default: break;
  }
  requestPaint();
}

void BrushLibraryPopup::menuPointer(double x, double y, bool press) {
  const RectD box = menuRect();
  const bool inBox = x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h;
  if (!inBox) {
    if (press) closeMenu();
    return;
  }
  const int row = std::clamp(static_cast<int>((y - box.y - 4.0) / bx::kMenuRow), 0, static_cast<int>(menu_.labels.size()) - 1);
  if (row != menu_.selected) {
    menu_.selected = row;
    requestPaint();
  }
  if (press) runMenuAction(row);
}

void BrushLibraryPopup::runMenuAction(int row) {
  if (row < 0 || row >= static_cast<int>(menu_.actions.size())) return;
  const uint32_t brush = menu_.brush;
  const int action = menu_.actions[static_cast<size_t>(row)];
  closeMenu();
  if (brush >= model_.size()) return;
  switch (action) {
    case kActionPick:
      pickBrush(brush);
      break;
    case kActionFavourite: toggleFavourite(brush); break;
    case kActionAssign: openAssign(brush); break;
    case kActionClear: {
      const cb::LetterResult result = model_.setUserLetter(model_.brushes()[brush].id, {});
      notice_ = result.message;
      break;
    }
    default: break;
  }
  requestPaint();
}

// ---- Assign letter popover ----------------------------------------------------------------------

void BrushLibraryPopup::openAssign(uint32_t brush) {
  if (brush >= model_.size()) return;
  menu_ = {};
  assign_ = {};
  assign_.open = true;
  assign_.brush = brush;
  const std::string current = model_.userLetter(model_.brushes()[brush].id);
  assign_.feedback = current.empty() ? "Press a letter or digit." : "Now " + current + ". Press another letter, or Delete to remove it.";
  requestPaint();
}

RectD BrushLibraryPopup::assignRect() const {
  // Centred horizontally over the highlighted tile when it is visible, else over the grid.
  const double width = std::min(bx::kAssignWidth, widthNow() - 16.0);
  double x = (widthNow() - width) * 0.5;
  double y = bx::kGridTop + 24.0;
  if (assign_.open) {
    for (size_t t = 0; t < result_.tiles.size(); ++t) {
      if (result_.tiles[t].brush != assign_.brush) continue;
      const RectD tile = tileRect(t);
      if (tile.w <= 0.0) break;
      const core::layout::Rect self = ui().absRect(id());
      x = tile.x - self.x + tile.w * 0.5 - width * 0.5;
      const double below = tile.y - self.y + tile.h + 4.0;
      y = below + bx::kAssignHeight <= heightNow() - bx::kFooterHeight ? below : tile.y - self.y - bx::kAssignHeight - 4.0;
      break;
    }
  }
  x = std::clamp(x, 8.0, std::max(8.0, widthNow() - width - 8.0));
  y = std::clamp(y, 8.0, std::max(8.0, heightNow() - bx::kAssignHeight - 8.0));
  return abs(x, y, width, bx::kAssignHeight);
}

void BrushLibraryPopup::assignKey(Event& e) {
  switch (e.key) {
    case Key::Escape:
      assign_ = {};
      break;
    case Key::Enter:
      if (!e.repeat) applyAssign();
      return;
    case Key::Delete:
    case Key::Backspace:
      assign_.candidate = 0;
      assign_.clear = true;
      assign_.feedback = "The letter is removed: the brush answers to the first letter of its name again.";
      break;
    default: return;
  }
  requestPaint();
}

void BrushLibraryPopup::assignCharacter(char32_t cp) {
  const char32_t folded = cb::foldCodePoint(cp);
  if (!cb::isKeyCharacter(folded)) {
    assign_.feedback = "That key is not a letter or digit.";
    requestPaint();
    return;
  }
  assign_.candidate = folded;
  assign_.clear = false;
  const size_t shared = model_.sharingLetter(upper(folded), assign_.brush);
  const std::string letter = upper(folded);
  if (shared == 0) {
    assign_.feedback = letter + " is free: typing it picks this brush at once. Enter assigns.";
  } else {
    assign_.feedback = std::to_string(shared) + (shared == 1 ? " other brush starts" : " other brushes start") + " with " + letter + ": typing it lists " + std::to_string(shared + 1) +
                       ". Enter assigns anyway.";
  }
  requestPaint();
}

void BrushLibraryPopup::applyAssign() {
  const uint32_t brush = assign_.brush;
  if (brush >= model_.size() || (assign_.candidate == 0 && !assign_.clear)) {
    assign_ = {};
    requestPaint();
    return;
  }
  const std::string brushId = model_.brushes()[brush].id;
  std::string letter;
  if (!assign_.clear) cb::appendUtf8(letter, assign_.candidate);
  assign_ = {};
  const cb::LetterResult result = model_.setUserLetter(brushId, letter);
  notice_ = result.message;
  requestPaint();
}

}  // namespace r1ui::widgets
