// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: pointer, keyboard and rename handling of ThumbnailGrid: selection rules (spec 08 45-53, 57-66),
//   type-ahead, Ctrl + wheel zoom, wheel scrolling, the scrollbar thumb, the drag-out start, the
//   context-menu and navigation hooks, and the inline rename box.
// Invariants: every handler re-checks that the widget is alive after calling out (callbacks may
//   destroy it); a press that started a gesture always ends it (release or capture loss); the
//   selection is changed only through commitSelection so listeners are notified once per action.
// Callers: UiContext event routing.
#include <algorithm>
#include <cmath>

#include "r1ui/text/Utf8.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/thumbnailgrid/ThumbnailGrid.h"

namespace r1ui::widgets {

namespace {

namespace events = core::events;
using thumbs::kNone;

bool mod(uint8_t m, uint8_t bit) { return (m & bit) != 0; }
bool ctrlOf(uint8_t m) { return mod(m, events::Mod::kCtrl) || mod(m, events::Mod::kMeta); }

constexpr double kWheelPixelsPerNotch = 72.0;

}  // namespace

// ---- hover, cursor, tooltip -------------------------------------------------------------------

size_t ThumbnailGrid::cursorIndex() const { return hasCursor_ ? indexOfKey(cursor_) : kNone; }

void ThumbnailGrid::updateHover(double x, double y) {
  pointerX_ = x;
  pointerY_ = y;
  pointerInside_ = true;
  const size_t index = thumbs::hitTest(computeMetrics(), x, y + scroll_);
  if (index != hoverIndex_) {
    hoverIndex_ = index;
    requestPaint();
  }
}

void ThumbnailGrid::onPointerLeave(Event&) {
  pointerInside_ = false;
  if (hoverIndex_ != kNone) requestPaint();
  hoverIndex_ = kNone;
}

Cursor ThumbnailGrid::cursor() const { return hoverIndex_ != kNone && pointerInside_ ? Cursor::Pointer : Cursor::Default; }

std::string_view ThumbnailGrid::tooltipText() const {
  if (hoverIndex_ == kNone || !pointerInside_ || hoverIndex_ >= shownCount()) return {};
  fetch(hoverIndex_, scratchItem_);
  tooltipScratch_ = tooltipFor ? tooltipFor(scratchItem_) : scratchItem_.name + (scratchItem_.typeLabel.empty() ? std::string() : "\n" + scratchItem_.typeLabel);
  return tooltipScratch_;
}

// ---- selection helpers ------------------------------------------------------------------------

void ThumbnailGrid::selectOnly(uint64_t key) {
  anchor_ = cursor_ = key;
  hasCursor_ = true;
  commitSelection({key});
}

std::vector<uint64_t> ThumbnailGrid::shownKeysBetween(size_t a, size_t b) const {
  std::vector<uint64_t> keys;
  const size_t n = shownCount();
  if (n == 0) return keys;
  const size_t lo = std::min({a, b, n - 1});
  const size_t hi = std::min(std::max(a, b), n - 1);
  keys.reserve(hi - lo + 1);
  GridItem item;
  for (size_t i = lo; i <= hi; ++i) {
    fetch(i, item);
    keys.push_back(item.key);
  }
  return keys;
}

void ThumbnailGrid::selectRange(size_t from, size_t to, bool additive) {
  std::unordered_set<uint64_t> next;
  if (additive) next = selected_;
  for (const uint64_t k : shownKeysBetween(from, to)) next.insert(k);
  commitSelection(std::move(next));
}

void ThumbnailGrid::moveCursor(size_t index, uint8_t modifiers) {
  if (index >= shownCount()) return;
  const uint64_t key = keyAt(index);
  size_t anchorIndex = hasCursor_ ? indexOfKey(anchor_) : kNone;
  if (anchorIndex == kNone) anchorIndex = 0;  // spec 08 edge case 6: an anchor that is gone starts at the first item
  if (mod(modifiers, events::Mod::kShift)) {
    selectRange(anchorIndex, index, ctrlOf(modifiers));  // rule 58: the range replaces, with Ctrl it is added
  } else if (ctrlOf(modifiers)) {
    std::unordered_set<uint64_t> next = selected_;
    next.insert(key);
    anchor_ = key;
    commitSelection(std::move(next));
  } else {
    selectOnly(key);
  }
  cursor_ = key;
  hasCursor_ = true;
  setScroll(thumbs::revealScroll(computeMetrics(), scroll_, viewportHeight(), index, false));
  requestPaint();
}

void ThumbnailGrid::activate() {
  const std::vector<uint64_t> keys = selectedKeys();
  if (!keys.empty() && onActivate) onActivate(keys);
}

void ThumbnailGrid::typeAheadJump(char32_t cp) {
  if (shownCount() == 0) return;
  if (!typeAhead_.type(cp, ui().now())) return;
  const size_t current = cursorIndex();
  const size_t match = thumbs::typeAheadMatch(typeAhead_.prefix(), shownCount(), current, [this](size_t i) -> std::string_view {
    fetch(i, scratchItem_);
    return scratchItem_.name;
  });
  if (match == kNone || match == current) return;
  moveCursor(match, 0);
}

// ---- pointer ----------------------------------------------------------------------------------

void ThumbnailGrid::onPointerDown(Event& e) {
  if (e.button == events::Button::X1 || e.button == events::Button::X2) {
    e.markHandled();
    if (e.button == events::Button::X1 && onNavigateBack) onNavigateBack();
    else if (e.button == events::Button::X2 && onNavigateForward) onNavigateForward();
    return;
  }
  if (e.button != events::Button::Left && e.button != events::Button::Right) return;
  e.markHandled();
  if (!focused()) ui().router().focus(id(), events::FocusReason::Pointer);
  press_ = Press{};
  press_.button = e.button;
  press_.x = e.x;
  press_.y = e.y;
  press_.modifiers = e.modifiers;
  press_.active = true;
  ui().router().capturePointer(id());
  if (e.button == events::Button::Right) return;  // handled on release (rule 51)

  // ---- scrollbar ----
  thumbs::Rect track;
  thumbs::Rect thumb;
  if (scrollbarThumb(track, thumb) && e.localX >= track.x - 6.0) {
    draggingScrollbar_ = true;
    press_.active = false;
    const double range = computeMetrics().contentHeight - viewportHeight();
    if (e.localY >= thumb.y && e.localY <= thumb.y + thumb.h) {
      scrollbarGrab_ = e.localY - thumb.y;
    } else {  // a click in the track moves the thumb there
      scrollbarGrab_ = thumb.h * 0.5;
      const double t = std::clamp((e.localY - scrollbarGrab_ - track.y) / std::max(1.0, track.h - thumb.h), 0.0, 1.0);
      setScroll(t * range);
    }
    return;
  }

  const thumbs::Metrics m = computeMetrics();
  const size_t index = thumbs::hitTest(m, e.localX, e.localY + scroll_);
  if (index == kNone) {
    if (!mod(e.modifiers, events::Mod::kShift) && !ctrlOf(e.modifiers)) clearSelection();  // rule 50
    press_.active = false;
    return;
  }
  const uint64_t key = keyAt(index);
  press_.index = index;
  press_.key = key;
  press_.wasSelected = selected_.count(key) != 0;
  const core::tree::WidgetId self = id();
  if (mod(e.modifiers, events::Mod::kShift)) {
    // Rule 48: everything between the anchor and the pressed item is added; the anchor stays.
    size_t anchorIndex = hasCursor_ ? indexOfKey(anchor_) : kNone;
    if (anchorIndex == kNone) anchorIndex = 0;
    selectRange(anchorIndex, index, true);
    cursor_ = key;
    hasCursor_ = true;
  } else if (ctrlOf(e.modifiers)) {
    std::unordered_set<uint64_t> next = selected_;
    if (!next.erase(key)) next.insert(key);
    anchor_ = cursor_ = key;
    hasCursor_ = true;
    commitSelection(std::move(next));
  } else if (!press_.wasSelected) {
    selectOnly(key);  // rule 45: on press
  } else {
    anchor_ = cursor_ = key;  // rule 46: a selected item keeps the selection until release
    hasCursor_ = true;
  }
  if (!ui().alive(self)) return;
  requestPaint();
}

void ThumbnailGrid::onPointerMove(Event& e) {
  if (draggingScrollbar_) {
    thumbs::Rect track;
    thumbs::Rect thumb;
    if (scrollbarThumb(track, thumb)) {
      const double range = computeMetrics().contentHeight - viewportHeight();
      const double t = std::clamp((e.localY - scrollbarGrab_ - track.y) / std::max(1.0, track.h - thumb.h), 0.0, 1.0);
      setScroll(t * range);
    }
    return;
  }
  updateHover(e.localX, e.localY);
  if (!press_.active || press_.dragStarted || press_.button != events::Button::Left) return;
  const double threshold = ui().router().config().dragThreshold;
  if (std::hypot(e.x - press_.x, e.y - press_.y) <= threshold) return;
  if (selected_.count(press_.key) == 0) return;
  press_.dragStarted = true;
  DragRequest request;
  request.keys = selectedKeys();
  request.x = press_.x;
  request.y = press_.y;
  const core::tree::WidgetId self = id();
  if (onDragStart) onDragStart(request);
  if (!ui().alive(self)) return;
}

void ThumbnailGrid::onPointerUp(Event& e) {
  const core::tree::WidgetId self = id();
  if (draggingScrollbar_ && e.button == events::Button::Left) {
    draggingScrollbar_ = false;
    requestPaint();
    return;
  }
  if (!press_.active || e.button != press_.button) return;
  const Press press = press_;
  press_ = Press{};
  if (press.button == events::Button::Left) {
    // Rule 46: a press on a selected item of a multi-selection collapses to it on release, unless dragged.
    if (!press.dragStarted && press.wasSelected && !mod(press.modifiers, events::Mod::kShift) && !ctrlOf(press.modifiers) && selected_.size() > 1) selectOnly(press.key);
    return;
  }
  // Right button: select what is under the pointer when it is not selected, then ask for the menu (rule 51 / 52).
  const size_t index = thumbs::hitTest(computeMetrics(), e.localX, e.localY + scroll_);
  GridContext context;
  context.x = e.x;
  context.y = e.y;
  if (index != kNone) {
    context.target = GridContext::Target::Item;
    context.key = keyAt(index);
    if (selected_.count(context.key) == 0) selectOnly(context.key);
  } else if (!mod(press.modifiers, events::Mod::kShift) && !ctrlOf(press.modifiers)) {
    clearSelection();
  }
  if (!ui().alive(self)) return;
  if (onContextMenu) onContextMenu(context);
}

void ThumbnailGrid::onCaptureLost(Event&) {
  press_ = Press{};
  draggingScrollbar_ = false;
}

void ThumbnailGrid::onDoubleClick(Event& e) {
  if (e.button != events::Button::Left) return;
  const size_t index = thumbs::hitTest(computeMetrics(), e.localX, e.localY + scroll_);
  if (index == kNone) return;
  e.markHandled();
  const uint64_t key = keyAt(index);  // rule 54: the item under the pointer, not the whole selection
  if (onActivate) onActivate({key});
}

void ThumbnailGrid::onPointerWheel(Event& e) {
  if (!std::isfinite(e.wheelY) || e.wheelY == 0.0) return;
  e.markHandled();
  if (ctrlOf(e.modifiers)) {
    stepZoom(e.wheelY > 0.0 ? 1 : -1);  // rule 9: one stop per notch, wheel up goes larger; the list does not scroll
    return;
  }
  if (renaming_) cancelRename();
  setScroll(scroll_ - std::clamp(e.wheelY, -20.0, 20.0) * kWheelPixelsPerNotch);
}

void ThumbnailGrid::onFocusIn(Event&) { requestPaint(); }
void ThumbnailGrid::onFocusOut(Event&) { requestPaint(); }

// ---- keyboard ---------------------------------------------------------------------------------

void ThumbnailGrid::onKeyDown(Event& e) {
  using events::Key;
  const uint8_t m = e.modifiers;
  const bool ctrl = ctrlOf(m);
  const int code = static_cast<int>(e.key);
  if (mod(m, events::Mod::kAlt) && e.key != Key::Backspace) return;  // rule 63: Alt changes nothing
  thumbs::Nav nav{};
  bool isNav = true;
  switch (e.key) {
    case Key::Left: nav = thumbs::Nav::Left; break;
    case Key::Right: nav = thumbs::Nav::Right; break;
    case Key::Up: nav = thumbs::Nav::Up; break;
    case Key::Down: nav = thumbs::Nav::Down; break;
    case Key::Home: nav = thumbs::Nav::Home; break;
    case Key::End: nav = thumbs::Nav::End; break;
    case Key::PageUp: nav = thumbs::Nav::PageUp; break;
    case Key::PageDown: nav = thumbs::Nav::PageDown; break;
    default: isNav = false; break;
  }
  if (isNav) {
    if (mode_ == thumbs::ViewMode::List && (nav == thumbs::Nav::Left || nav == thumbs::Nav::Right)) return;
    e.markHandled();
    const size_t to = thumbs::navigate(computeMetrics(), cursorIndex(), nav, viewportHeight());
    if (to != kNone) moveCursor(to, m);
    return;
  }
  if (e.key == Key::Enter || (ctrl && code == 'E')) {
    e.markHandled();
    activate();
  } else if (code == static_cast<int>(Key::F1) + 1) {  // F2
    e.markHandled();
    const std::vector<uint64_t> keys = selectedKeys();
    if (keys.size() == 1) beginRename(keys.front());
  } else if (e.key == Key::Delete) {
    e.markHandled();
    const std::vector<uint64_t> keys = selectedKeys();
    if (!keys.empty() && onDeleteRequested) onDeleteRequested(keys);
  } else if (e.key == Key::Space) {
    e.markHandled();
    if (ctrl && hasCursor_) {
      std::unordered_set<uint64_t> next = selected_;
      if (!next.erase(cursor_)) next.insert(cursor_);
      commitSelection(std::move(next));
    } else if (typeAhead_.active(ui().now())) {
      typeAheadJump(U' ');  // a space inside a typed name
    } else {
      const std::vector<uint64_t> keys = selectedKeys();
      if (!keys.empty() && onPreview) onPreview(keys);
    }
  } else if (ctrl && code == 'A') {
    e.markHandled();
    selectAll();
  } else if (ctrl && e.key == Key::Backspace) {
    e.markHandled();
    if (onNavigateParent) onNavigateParent();
  } else if (ctrl && mod(m, events::Mod::kShift) && code == 'N') {
    e.markHandled();
    if (onNewFolder) onNewFolder();
  } else if (e.key == Key::Escape) {
    typeAhead_.reset();
  }
}

void ThumbnailGrid::onTextInput(Event& e) {
  if (ctrlOf(e.modifiers) || mod(e.modifiers, events::Mod::kAlt) || renaming_) return;
  if (e.codePoint == U' ' && !typeAhead_.active(ui().now())) return;  // Space previews; it only extends a prefix
  e.markHandled();
  typeAheadJump(e.codePoint);
}

// ---- rename -----------------------------------------------------------------------------------

PickerEntry* ThumbnailGrid::renameEntry() const { return renaming_ ? ui().objectAs<PickerEntry>(renameEntry_) : nullptr; }

void ThumbnailGrid::beginRename(uint64_t key) {
  const size_t index = indexOfKey(key);
  if (index == kNone) return;
  GridItem item;
  fetch(index, item);
  if (item.readOnly) return;
  cancelRename();
  setScroll(thumbs::revealScroll(computeMetrics(), scroll_, viewportHeight(), index, false));
  const thumbs::Metrics m = computeMetrics();
  const thumbs::Rect tile = thumbs::itemRect(m, index);
  const thumbs::Rect label = thumbs::labelRect(m, tile);
  PickerEntry& entry = ui().create<PickerEntry>(id(), PickerEntry::Look::Input);
  renameEntry_ = entry.id();
  renameKey_ = key;
  renaming_ = true;
  renameInvalid_ = false;
  core::layout::Style& s = entry.style();
  s.position = core::layout::Position::Absolute;
  s.inset[core::layout::kLeft] = core::layout::Length::px(std::max(0.0, label.x - 2.0));
  s.inset[core::layout::kTop] = core::layout::Length::px(label.y + (m.mode == thumbs::ViewMode::Grid ? 0.0 : (label.h - 22.0) * 0.5) - scroll_);
  s.width = core::layout::Length::px(std::max(60.0, label.w + 4.0));
  s.height = core::layout::Length::px(22.0);
  entry.setFontSize(12.0);
  entry.setText(item.name);
  entry.setAccessibleName("Rename");
  entry.onCommit = [this, key, original = item.name](std::string_view text) {
    renameInvalid_ = false;
    const std::string name(text);
    if (name == original) return true;
    std::string error;
    const bool ok = onRename ? onRename(key, name, error) : true;
    if (ok) {
      finishRename(true, name);
      return true;
    }
    // Rule 39: the box stays open with an error; an invalid text is discarded when the user clicks away.
    renameInvalid_ = true;
    if (PickerEntry* en = renameEntry()) {
      en->setInvalid(true);
      en->setTooltip(error.empty() ? "This name cannot be used" : error);
    }
    return true;
  };
  entry.onSubmit = [this] {
    if (renaming_ && !renameInvalid_) finishRename(false, {});
  };
  entry.onCancel = [this] { cancelRename(); };
  entry.onBlurred = [this] {
    if (renaming_) cancelRename();
  };
  ui().router().focus(entry.id(), events::FocusReason::Keyboard);
  requestPaint();
}

void ThumbnailGrid::finishRename(bool commit, const std::string&) {
  if (!renaming_) return;
  const uint64_t key = renameKey_;
  const core::tree::WidgetId entry = renameEntry_;
  renaming_ = false;
  renameKey_ = 0;
  renameInvalid_ = false;
  if (ui().alive(entry)) ui().destroy(entry);
  if (ui().alive(id())) {
    ui().router().focus(id(), events::FocusReason::Program);
    if (commit) {
      const size_t index = indexOfKey(key);
      if (index != kNone) {
        selectOnly(key);
        setScroll(thumbs::revealScroll(computeMetrics(), scroll_, viewportHeight(), index, false));
      }
    }
    requestPaint();
  }
}

void ThumbnailGrid::cancelRename() {
  if (!renaming_) return;
  finishRename(false, {});
}

}  // namespace r1ui::widgets
