// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the keyboard and pointer handling of BrushLibraryPopup: every key belongs to the popup while it
//   is open (so no application command can fire), typed characters narrow the list, the arrows move the
//   highlight, the mouse hovers, clicks, scrolls and drags the scrollbar.
// Invariants: every key and text event is marked handled and stopped, whatever it does; pointer capture is
//   released on every exit path (release, capture loss, popover opening); the character produced by the key
//   that opened the library never reaches the text; after a pick or close request the popup touches no more
//   state (the host may destroy it).
// Callers: the Router (events), tests with synthetic input.
#include <algorithm>
#include <cctype>
#include <cmath>

#include "BrushPopupGeometry.h"
#include "r1ui/commands/brushes/BrushLetters.h"
#include "r1ui/widgets/brushes/BrushLibraryPopup.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace cb = commands::brushes;
namespace bx = brushes;
using core::events::Button;
using core::events::Key;
namespace Mod = core::events::Mod;

namespace {

constexpr double kWheelPixelsPerNotch = 64.0;
constexpr Key kF2 = static_cast<Key>(113);
constexpr Key kF = static_cast<Key>('F');

bool plainLetterOrDigit(Key key, uint8_t modifiers) {
  const unsigned k = static_cast<unsigned>(key);
  const bool character = (k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9');
  return character && (modifiers & (Mod::kCtrl | Mod::kAlt | Mod::kMeta)) == 0;
}

}  // namespace

// ---- keyboard -----------------------------------------------------------------------------------

void BrushLibraryPopup::onKeyDown(Event& e) {
  e.markHandled();
  e.stopPropagation();
  // The press of another key means the key that opened the library was released.
  if (ignoreChar_ != 0 && !(plainLetterOrDigit(e.key, e.modifiers) && static_cast<char32_t>(std::tolower(static_cast<int>(e.key))) == ignoreChar_)) ignoreChar_ = 0;
  if (assign_.open) {
    assignKey(e);
    return;
  }
  if (menu_.open) {
    menuKey(e);
    return;
  }
  handleControlKey(e);
}

bool BrushLibraryPopup::handleControlKey(Event& e) {
  const uint8_t mods = e.modifiers;
  const bool ctrl = (mods & Mod::kCtrl) != 0;
  const bool alt = (mods & Mod::kAlt) != 0;
  if (hooks_.isOpenChord && hooks_.isOpenChord(e.key, mods)) {
    if (e.repeat) return true;  // holding the key that opened the library must not close it again
    // A plain letter closes only before anything is typed (afterwards it is a letter of a name); with
    // the search field active it is text. Shift+letter (a different chord) always types the letter.
    if (!plainLetterOrDigit(e.key, mods) || (mode_ == cb::QueryMode::TypeToPick && text_.empty())) {
      close();
      return true;
    }
  }
  notice_.clear();
  switch (e.key) {
    case Key::Escape: close(); return true;
    case Key::Enter:
      if (!e.repeat) pickHighlighted();
      return true;
    case Key::Tab: toggleMode(); return true;
    case Key::Backspace:
      if (ctrl) clearText();
      else eraseCharacter();
      return true;
    case Key::Delete: clearText(); return true;
    case Key::Left:
    case Key::Right:
      if (ctrl) stepChip(e.key == Key::Left ? -1 : 1);
      else moveHighlight(e.key);
      return true;
    case Key::Up:
    case Key::Down:
    case Key::Home:
    case Key::End:
    case Key::PageUp:
    case Key::PageDown:
      if (!alt) moveHighlight(e.key);
      return true;
    default: break;
  }
  if (e.key == kF2) {
    if (const auto brush = highlightedBrush()) openAssign(*brush);
    return true;
  }
  if (ctrl && e.key == kF && !e.repeat) {
    if (const auto brush = highlightedBrush()) toggleFavourite(*brush);
    return true;
  }
  requestPaint();
  return true;
}

void BrushLibraryPopup::onKeyUp(Event& e) {
  e.markHandled();
  e.stopPropagation();
  ignoreChar_ = 0;
}

void BrushLibraryPopup::onTextInput(Event& e) {
  e.markHandled();
  e.stopPropagation();
  if ((e.modifiers & (Mod::kCtrl | Mod::kAlt | Mod::kMeta)) != 0) return;
  const char32_t cp = e.codePoint;
  if (ignoreChar_ != 0 && cb::foldCodePoint(cp) == ignoreChar_) return;
  if (assign_.open) {
    assignCharacter(cp);
    return;
  }
  if (menu_.open) return;
  typeCharacter(cp);
}

// ---- pointer ------------------------------------------------------------------------------------

void BrushLibraryPopup::setHover(int tile, bool star) {
  if (tile == hoverTile_ && star == hoverStar_) return;
  hoverTile_ = tile;
  hoverStar_ = star;
  requestPaint();
}

void BrushLibraryPopup::onPointerMove(Event& e) {
  const double x = e.x;
  const double y = e.y;
  if (draggingScrollbar_) {
    dragScrollbar(y);
    return;
  }
  if (menu_.open) {
    menuPointer(x, y, false);
    return;
  }
  if (assign_.open) return;
  const int tile = tileAt(x, y);
  const bool star = tile >= 0 && [&] {
    const core::layout::RectD s = starRect(static_cast<size_t>(tile));
    return x >= s.x && x < s.x + s.w && y >= s.y && y < s.y + s.h;
  }();
  setHover(tile, star);
  if (tile >= 0) setHighlight(tile);  // hover highlights
  const int chip = chipAt(x, y);
  const core::layout::RectD mode = modeRect();
  const core::layout::RectD option = optionRect();
  const bool overMode = x >= mode.x && x < mode.x + mode.w && y >= mode.y && y < mode.y + mode.h;
  const bool overOption = x >= option.x && x < option.x + option.w && y >= option.y && y < option.y + option.h;
  if (chip != hoverChip_ || overMode != hoverMode_ || overOption != hoverOption_) {
    hoverChip_ = chip;
    hoverMode_ = overMode;
    hoverOption_ = overOption;
    requestPaint();
  }
}

void BrushLibraryPopup::onPointerLeave(Event&) {
  setHover(-1, false);
  if (hoverChip_ != -1 || hoverMode_ || hoverOption_) {
    hoverChip_ = -1;
    hoverMode_ = false;
    hoverOption_ = false;
    requestPaint();
  }
}

void BrushLibraryPopup::onPointerDown(Event& e) {
  e.markHandled();
  ui().focusWidget(id());  // the popup keeps the keyboard whatever was clicked
  notice_.clear();
  const double x = e.x;
  const double y = e.y;
  pressTile_ = -1;
  pressStar_ = false;
  if (menu_.open) {
    menuPointer(x, y, true);
    return;
  }
  if (assign_.open) {
    const core::layout::RectD box = assignRect();
    if (!(x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h)) assign_.open = false;
    requestPaint();
    return;
  }
  if (e.button == Button::Right) {
    const int tile = tileAt(x, y);
    if (tile >= 0) {
      setHighlight(tile);
      openMenu(result_.tiles[static_cast<size_t>(tile)].brush, x, y);
    }
    return;
  }
  if (e.button != Button::Left) return;
  if (onScrollbar(x, y)) {
    const core::layout::RectD grid = gridRect();
    const double content = layout_->contentHeight();
    const double thumb = std::max(24.0, grid.h * grid.h / content);
    const double thumbTop = grid.y + (grid.h - thumb) * (layout_->maxScroll(grid.h) > 0.0 ? scroll_ / layout_->maxScroll(grid.h) : 0.0);
    scrollbarGrab_ = y >= thumbTop && y < thumbTop + thumb ? y - thumbTop : thumb * 0.5;
    draggingScrollbar_ = true;
    ui().router().capturePointer(id());
    dragScrollbar(y);
    return;
  }
  const core::layout::RectD mode = modeRect();
  if (x >= mode.x && x < mode.x + mode.w && y >= mode.y && y < mode.y + mode.h) {
    toggleMode();
    return;
  }
  const core::layout::RectD option = optionRect();
  if (x >= option.x && x < option.x + option.w && y >= option.y && y < option.y + option.h) {
    model_.setPickOnUniqueOption(!model_.pickOnUniqueOption());
    requestPaint();
    return;
  }
  if (const int chip = chipAt(x, y); chip >= 0) {
    selectChip(static_cast<size_t>(chip));
    return;
  }
  if (const int tile = tileAt(x, y); tile >= 0) {
    pressTile_ = tile;
    const core::layout::RectD s = starRect(static_cast<size_t>(tile));
    pressStar_ = x >= s.x && x < s.x + s.w && y >= s.y && y < s.y + s.h;
  }
}

void BrushLibraryPopup::onPointerUp(Event& e) {
  e.markHandled();
  if (draggingScrollbar_) {
    draggingScrollbar_ = false;
    ui().router().releaseCapture();
    return;
  }
  if (e.button != Button::Left || pressTile_ < 0) return;
  const int tile = tileAt(e.x, e.y);
  const int pressed = pressTile_;
  const bool star = pressStar_;
  pressTile_ = -1;
  pressStar_ = false;
  if (tile != pressed || tile >= static_cast<int>(result_.tiles.size())) return;
  if (star) {
    toggleFavourite(result_.tiles[static_cast<size_t>(tile)].brush);
    return;
  }
  pickTile(static_cast<size_t>(tile));
}

void BrushLibraryPopup::onCaptureLost(Event&) {
  draggingScrollbar_ = false;
  pressTile_ = -1;
  pressStar_ = false;
}

void BrushLibraryPopup::onPointerWheel(Event& e) {
  if (!std::isfinite(e.wheelY) || e.wheelY == 0.0) return;
  e.markHandled();
  const core::layout::RectD chips = abs(bx::chipsBox(widthNow()).x, bx::chipsBox(widthNow()).y, bx::chipsBox(widthNow()).w, bx::chipsBox(widthNow()).h);
  if (e.x >= chips.x && e.x < chips.x + chips.w && e.y >= chips.y && e.y < chips.y + chips.h) {
    const double total = chips_.empty() ? 0.0 : chips_.back().x + chips_.back().w;
    chipScroll_ = std::clamp(chipScroll_ - std::clamp(e.wheelY, -20.0, 20.0) * 40.0, 0.0, std::max(0.0, total - chips.w));
    requestPaint();
    return;
  }
  if (menu_.open || assign_.open) return;
  scrollTo(scroll_ - std::clamp(e.wheelY, -20.0, 20.0) * kWheelPixelsPerNotch);
}

void BrushLibraryPopup::dragScrollbar(double y) {
  const core::layout::RectD grid = gridRect();
  const double content = layout_->contentHeight();
  if (content <= grid.h) return;
  const double thumb = std::max(24.0, grid.h * grid.h / content);
  const double travel = std::max(1.0, grid.h - thumb);
  const double fraction = std::clamp((y - grid.y - scrollbarGrab_) / travel, 0.0, 1.0);
  scrollTo(fraction * layout_->maxScroll(grid.h));
}

Cursor BrushLibraryPopup::cursor() const {
  if (hoverTile_ >= 0 || hoverChip_ >= 0 || hoverMode_ || hoverOption_) return Cursor::Pointer;
  return Cursor::Default;
}

std::string_view BrushLibraryPopup::tooltipText() const {
  if (hoverTile_ < 0 || hoverTile_ >= static_cast<int>(result_.tiles.size()) || menu_.open || assign_.open) return {};
  const cb::TileInfo& tile = result_.tiles[static_cast<size_t>(hoverTile_)];
  const cb::BrushInfo& info = model_.brushes()[tile.brush];
  tooltipScratch_ = info.name + " (" + info.category + ")";
  const std::string key = model_.keyText(tile.brush);
  if (!key.empty()) tooltipScratch_ += "\nKey: " + key;
  if (!info.description.empty()) tooltipScratch_ += "\n" + info.description;
  return tooltipScratch_;
}

}  // namespace r1ui::widgets
