// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the base class of every widget: the object attached to a node of the WidgetTree that holds
//   the widget's behaviour and state (hover, pressed, focus, disabled, ...), its hooks (measure,
//   layout callback, paint, pointer / key / text / focus handlers), cursor, tooltip and accessible
//   name.
// Why: ui-core's tree::Widget is plain data (style, flags, rectangles). A widget is that node plus
//   one WidgetObject, owned by the UiContext and reachable from the node (userData slot +
//   EventHandler pointer), so layout, routing and invalidation stay data driven while widgets add
//   behaviour through virtual hooks only where they need it.
// Callers: UiContext (creates, owns, dispatches), concrete widgets (derive), tests.
// Lifetime: create widgets with UiContext::create<T>(parent, args...). The constructor must not
//   touch the tree (there is no id yet); configure the node's style and create child widgets in
//   onAttached(). Never delete a widget; call UiContext::destroy(id). A destroyed widget's object
//   lives until the UI thread is outside any event dispatch, so a handler may destroy its own
//   widget (hide a menu from its own click) without a use-after-free; after destroy the ids stale
//   out, so re-check `ui().alive(id)` after calling code that may destroy.
// Paint contract: paint() draws only inside the widget's absolute rectangle (or deliberately
//   outside it, for shadows and focus rings) using PaintContext; it must not mutate the tree or
//   request layout. Children are painted by the framework after paint() and before paintOver().
// Event contract: the default onEvent() first updates the state flags (hover on enter/leave,
//   pressed between left press and release or leave, focused / focusVisible on focus events) and
//   requests a repaint when a flag changed, then calls the matching on*() hook. Hooks run in the
//   Router's target and bubble phases; call event.markHandled() when the event was used, and
//   event.stopPropagation() to hide it from ancestors. A widget that needs the capture phase
//   overrides phases().
// State -> style: styleState() maps the flags to theme::State bits for StyleSheet::resolve (hover ->
//   kHover, pressed -> kActive, focused -> kFocus, disabled -> kDisabled, selected, mixed, bound,
//   invalid); override it to change the mapping (a button shows focus only when focusVisible).
// Threading: UI thread only.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "r1ui/core/events/Event.h"
#include "r1ui/core/events/EventHandler.h"
#include "r1ui/core/layout/Measure.h"
#include "r1ui/core/layout/Style.h"
#include "r1ui/core/tree/Widget.h"
#include "r1ui/core/tree/WidgetId.h"

namespace r1ui::widgets {

class UiContext;
class PaintContext;

// Mouse cursor wanted while the pointer is over a widget (the nearest ancestor-or-self that is not
// Default decides). The shell maps it to the OS cursor.
enum class Cursor : uint8_t {
  Default,
  Pointer,      // hand
  Text,
  ResizeHorizontal,
  ResizeVertical,
  ResizeNwSe,
  ResizeNeSw,
  Move,
  NotAllowed,
  Wait
};

namespace StateFlag {
inline constexpr uint16_t kHover = 1;
inline constexpr uint16_t kPressed = 2;
inline constexpr uint16_t kFocused = 4;
inline constexpr uint16_t kFocusVisible = 8;  // focused and the focus arrived by keyboard
inline constexpr uint16_t kDisabled = 16;
inline constexpr uint16_t kSelected = 32;
inline constexpr uint16_t kMixed = 64;
inline constexpr uint16_t kBound = 128;
inline constexpr uint16_t kInvalid = 256;
}  // namespace StateFlag

class WidgetObject : public core::events::EventHandler {
 public:
  WidgetObject() = default;
  ~WidgetObject() override = default;
  WidgetObject(const WidgetObject&) = delete;
  WidgetObject& operator=(const WidgetObject&) = delete;

  // ---- identity ----
  // Valid after attachment (from onAttached on). Short human name for logs and tests.
  virtual const char* typeName() const = 0;
  UiContext& ui() const { return *ui_; }
  core::tree::WidgetId id() const { return id_; }
  bool attached() const { return ui_ != nullptr; }
  // The tree node; never null while the widget is alive and attached.
  core::tree::Widget& node() const;
  // Layout style of the node. After changing it call requestLayout().
  core::layout::Style& style() const;

  // ---- state flags ----
  uint16_t state() const { return state_; }
  bool hasState(uint16_t flag) const { return (state_ & flag) != 0; }
  // Sets or clears a flag, calls onStateChanged and requests a repaint when it changed.
  void setState(uint16_t flag, bool on);
  bool hovered() const { return hasState(StateFlag::kHover); }
  bool pressed() const { return hasState(StateFlag::kPressed); }
  bool focused() const { return hasState(StateFlag::kFocused); }
  bool focusVisible() const { return hasState(StateFlag::kFocusVisible); }
  // Enabled = not disabled. A disabled widget (and its subtree) gets no pointer or focus events.
  bool enabled() const { return !hasState(StateFlag::kDisabled); }
  void setEnabled(bool enabled);
  void setFocusable(bool focusable);
  void setSelected(bool on) { setState(StateFlag::kSelected, on); }
  void setMixed(bool on) { setState(StateFlag::kMixed, on); }
  void setBound(bool on) { setState(StateFlag::kBound, on); }
  void setInvalid(bool on) { setState(StateFlag::kInvalid, on); }
  // theme::State bits for the style sheet (see header comment).
  virtual uint8_t styleState() const;

  // ---- text metadata ----
  // Tooltip text shown after the hover delay; the nearest ancestor with a non-empty one supplies it.
  virtual std::string_view tooltipText() const { return tooltip_; }
  void setTooltip(std::string text) { tooltip_ = std::move(text); }
  // Name for assistive technology (not consumed yet; stored so widgets can already provide it).
  virtual std::string_view accessibleName() const { return accessibleName_; }
  void setAccessibleName(std::string name) { accessibleName_ = std::move(name); }
  virtual Cursor cursor() const { return Cursor::Default; }

  // ---- invalidation helpers (forward to the Invalidator) ----
  void requestLayout();
  void requestPaint();
  // Opt in to onLayout() after every layout pass that ran.
  void setWantsLayoutCallback(bool wants);
  bool wantsLayoutCallback() const { return wantsLayout_; }

  // ---- lifecycle and layout hooks ----
  virtual void onAttached() {}
  // Before the node is destroyed (children are already gone or going); the id is still valid.
  virtual void onDetached() {}
  // Content size in logical pixels; only called when style().hasMeasure is true. Must be a pure
  // function of the widget's state (see core/layout/Measure.h) and must not mutate the tree.
  virtual core::layout::MeasureResult measure(const core::layout::MeasureInput& input);
  // After a layout pass: the rectangles of this widget and its children are final.
  virtual void onLayout() {}
  virtual void onStateChanged(uint16_t previous) { (void)previous; }

  // ---- painting ----
  // Opacity applied to this widget and its whole subtree (the style sheet's Opacity row for a
  // disabled widget, a fade). The framework pushes it around paint, children and paintOver.
  virtual float paintOpacity() const { return 1.0f; }
  virtual void paint(PaintContext& ctx) { (void)ctx; }
  // After the children (focus ring, scroll bars, overlays on the widget).
  virtual void paintOver(PaintContext& ctx) { (void)ctx; }

  // ---- events ----
  void onEvent(core::events::Event& event, core::events::Router& router) final;
  using Event = core::events::Event;
  virtual void onPointerEnter(Event&) {}
  virtual void onPointerLeave(Event&) {}
  virtual void onPointerDown(Event&) {}
  virtual void onPointerUp(Event&) {}
  virtual void onPointerMove(Event&) {}
  virtual void onPointerWheel(Event&) {}
  virtual void onClick(Event&) {}
  virtual void onDoubleClick(Event&) {}
  virtual void onDragStart(Event&) {}
  virtual void onCaptureLost(Event&) {}
  virtual void onKeyDown(Event&) {}
  virtual void onKeyUp(Event&) {}
  virtual void onTextInput(Event&) {}
  virtual void onFocusIn(Event&) {}
  virtual void onFocusOut(Event&) {}

 private:
  friend class UiContext;
  void bind(UiContext& ui, core::tree::WidgetId id);
  void updateStateFromEvent(Event& event);

  UiContext* ui_ = nullptr;
  core::tree::WidgetId id_;
  uint16_t state_ = 0;
  bool wantsLayout_ = false;
  std::string tooltip_;
  std::string accessibleName_;
};

}  // namespace r1ui::widgets
