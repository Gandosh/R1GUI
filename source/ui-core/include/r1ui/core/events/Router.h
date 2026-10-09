// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the stateful event router: pointer hit testing and delivery with capture/bubble phases,
//   hover tracking, pointer capture, click / double-click / drag-start synthesis, keyboard
//   focus (with Tab traversal) and keyboard / text routing.
// Why: this is the single place that turns raw input into widget events, so the ordering rules
//   (focused widget before ancestors before global shortcuts; capture before hit test; leave
//   before enter) are defined once and tested once.
// Callers: ui-platform feeds PointerInput / key calls; widget handlers call back into the
//   router (capturePointer, focus, ...). Calls: tree::WidgetTree, TreeQueries, EventHandler.
// Threading: UI thread only. Not thread-safe.
// Lifetime safety: the router stores WidgetIds only. Every entry point first re-validates
//   hover, capture and focus against the tree: a destroyed capturer or focused widget is
//   dropped (no event can be delivered to it), a hidden / disabled one is released with
//   CaptureLost / FocusOut. After each handler call ids are re-checked, so a handler may
//   destroy anything, including the widget it runs on. Nested dispatch deeper than
//   kMaxDispatchDepth is dropped.
// Pointer rules:
//  * Target = the capturing widget if there is one, else the topmost hit (hitTest).
//  * Hover is the chain root..leaf of the widget under the pointer (the capturer while
//    captured); Leave goes to widgets that left the chain (leaf first), Enter to new ones (root
//    first). While captured, hover is frozen on the capturer; releasing capture re-evaluates
//    hover at the last pointer position.
//  * capturePointer is accepted only while a button is held. It replaces an earlier capturer
//    (which receives CaptureLost). Capture ends automatically when the last button is released
//    or the capturer dies / is hidden.
//  * Click is synthesised on release of the press button when the press was not an accepted
//    drag; its target is the deepest common ancestor of the press target and the release hit.
//    Only the first button pressed starts a press; presses made while another button is held
//    deliver Down/Up but never Click. DoubleClick follows the second Click of a sequence
//    (same button and press target, within doubleClickMs and doubleClickDistance).
//  * DragStart is sent once per press to the press target (bubbling) when the pointer is
//    strictly more than dragThreshold px from the press point (spec 08 rule 1: exactly the
//    threshold is not a drag). A handler marks it handled to accept the drag, which suppresses
//    the Click. cancelPointerInteraction() (e.g. Escape during a drag, capture or focus lost to
//    another window) forgets the press, releases capture and clears the held-button state
//    without a click; a release that arrives later for a cancelled button is ignored.
//  * A left press that nobody handled focuses the innermost focusable widget at or above the
//    target (without focus indication), unless a handler already moved focus (spec 01 rules
//    1-3); pressing a non-focusable area keeps the previous focus (rule 8).
// Keyboard rules: key down goes to the focused widget then its ancestors (target + bubble,
//   disabled widgets skipped); if unused and the key is Tab without Ctrl/Alt/Meta, focus moves
//   next / previous (Shift); if still unused the GlobalKeyHandler gets it. Key up and text input
//   go to the focused chain only. Spatial arrow navigation and Escape policy belong to
//   higher layers (menus, drag managers) and are not implemented here.
#pragma once

#include <cstdint>
#include <vector>

#include "r1ui/core/events/Event.h"
#include "r1ui/core/events/EventHandler.h"
#include "r1ui/core/tree/WidgetTree.h"

namespace r1ui::core::events {

inline constexpr int kMaxDispatchDepth = 8;

struct RouterConfig {
  double dragThreshold = 5.0;        // px; a drag needs a distance strictly greater than this
  uint64_t doubleClickMs = 500;      // max time between clicks of a sequence
  double doubleClickDistance = 4.0;  // max distance between clicks of a sequence (px, inclusive)
};

class Router {
 public:
  Router(tree::WidgetTree& tree, tree::WidgetId root, RouterConfig config = {});

  void setRoot(tree::WidgetId root) { root_ = root; }
  tree::WidgetId root() const { return root_; }
  const RouterConfig& config() const { return config_; }
  // Non-finite or negative thresholds fall back to the defaults.
  void setConfig(const RouterConfig& config);
  void setGlobalKeyHandler(GlobalKeyHandler* handler) { global_ = handler; }
  void setFocusObserver(FocusObserver* observer) { observer_ = observer; }

  // ---- host input; each returns true when a handler marked the event handled ----
  bool pointerMove(const PointerInput& input);
  bool pointerDown(const PointerInput& input);
  bool pointerUp(const PointerInput& input);
  bool pointerWheel(const PointerInput& input);
  void pointerLeftWindow();
  bool keyDown(Key key, uint8_t modifiers, bool repeat, uint64_t timestampMs = 0);
  bool keyUp(Key key, uint8_t modifiers, uint64_t timestampMs = 0);
  // Invalid code points (0, surrogates, > U+10FFFF) are dropped.
  bool textInput(char32_t codePoint, uint8_t modifiers = Mod::kNone, uint64_t timestampMs = 0);

  // ---- state queries (validated: never return a dead widget) ----
  tree::WidgetId hovered() const;
  tree::WidgetId capturer() const;
  tree::WidgetId focused() const;
  bool focusVisible() const { return focusVisible_ && focused().valid(); }
  uint32_t heldButtons() const { return buttons_; }
  tree::WidgetId hitTest(double x, double y) const;

  // ---- pointer capture ----
  bool capturePointer(tree::WidgetId widget);
  void releaseCapture();
  void cancelPointerInteraction();

  // ---- focus ----
  // Moves focus if the widget is focusable; false (focus unchanged) otherwise.
  bool focus(tree::WidgetId widget, FocusReason reason = FocusReason::Program);
  void clearFocus();
  // Focus the next / previous widget in Tab order; false when nothing is focusable.
  bool focusNext(bool backwards = false);
  // The id to pass to restoreFocus later; invalid when nothing is focused.
  tree::WidgetId saveFocus() const { return focused(); }
  // Refocuses `saved` only if it still exists and is focusable (spec 01 rule 14).
  bool restoreFocus(tree::WidgetId saved) { return saved.valid() && focus(saved, FocusReason::Program); }

  // Re-validates hover, capture and focus after the tree changed (destroy, hide, disable) and
  // delivers the Leave / Enter / CaptureLost / FocusOut events that follow.
  void sync();

 private:
  struct Press {
    bool active = false;
    Button button = Button::None;
    tree::WidgetId target;
    double x = 0.0;
    double y = 0.0;
    uint32_t clickCount = 1;
    bool dragStarted = false;
    bool dragAccepted = false;
    uint32_t focusEpoch = 0;
  };
  struct LastClick {
    bool valid = false;
    Button button = Button::None;
    tree::WidgetId target;
    double x = 0.0;
    double y = 0.0;
    uint64_t timestampMs = 0;
    uint32_t count = 0;
  };
  enum class Delivery { PointerBubbling, KeyBubbling, TargetOnly };

  // delivery
  bool deliver(Event& event, tree::WidgetId target, Delivery mode);
  Event makePointerEvent(EventType type, const PointerInput& input) const;
  void validateCapture();
  void validateFocus();
  void updateHover(tree::WidgetId newLeaf);
  void refreshHoverFromLastPosition();
  uint32_t prospectiveClickCount(Button button, tree::WidgetId target, const PointerInput& input) const;
  void maybeStartDrag(const PointerInput& input);
  bool setFocus(tree::WidgetId next, FocusReason reason);
  void endCapture();

  tree::WidgetTree& tree_;
  tree::WidgetId root_;
  RouterConfig config_;
  GlobalKeyHandler* global_ = nullptr;
  FocusObserver* observer_ = nullptr;

  std::vector<tree::WidgetId> hoverChain_;  // root..leaf of the hovered widgets
  uint64_t hoverVersion_ = 0;               // tree structure version the chain was built at
  // Capacity kept between hover changes so a pointer move that crosses widgets allocates nothing.
  std::vector<tree::WidgetId> hoverSpareA_;
  std::vector<tree::WidgetId> hoverSpareB_;
  tree::WidgetId capture_;
  uint32_t buttons_ = 0;
  Press press_;
  LastClick lastClick_;
  bool havePointer_ = false;
  double lastX_ = 0.0;
  double lastY_ = 0.0;
  uint8_t lastModifiers_ = Mod::kNone;
  tree::WidgetId focus_;
  bool focusVisible_ = false;
  uint32_t focusEpoch_ = 0;
  int depth_ = 0;
};

}  // namespace r1ui::core::events
