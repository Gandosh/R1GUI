// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PieTrigger and pieSlotViews (PieTrigger.h).
// Invariants: at most one gesture runs; the pointer capture, the draw timer and the overlay exist only
//   while it runs (every exit path - release, Escape, capture loss, overlay dismissal, destruction -
//   cancels the timer, closes the overlay and clears the gesture); a command is run only after the
//   gesture state is gone, so a command that destroys this widget, opens a menu or throws cannot corrupt
//   it; predicates and callbacks that throw are contained.
// Callers: the host's widget tree, tests.
#include "r1ui/widgets/pie/PieTrigger.h"

#include <stdexcept>

#include "r1ui/commands/Text.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

using core::events::Button;
using core::events::EventType;
using core::events::Key;
using core::events::Phase;

// ---- slot views ---------------------------------------------------------------------------------

namespace {

bool safeCall(const std::function<bool()>& predicate, bool fallback) {
  if (!predicate) return fallback;
  try {
    return predicate();
  } catch (...) {
    return false;
  }
}

}  // namespace

std::vector<PieSlotView> pieSlotViews(const CommandServices& services, const commands::custommenu::CustomMenu& menu) {
  std::vector<PieSlotView> views;
  views.reserve(menu.entries.size());
  for (const commands::custommenu::MenuEntry& entry : menu.entries) {
    PieSlotView view;
    view.filled = !entry.commandId.empty();
    if (view.filled) {
      const commands::CommandDef* def = services.registry.find(entry.commandId);
      view.missing = def == nullptr;
      view.label = !entry.label.empty() ? entry.label : (def != nullptr ? def->label : entry.commandId);
      view.icon = !entry.icon.empty() ? entry.icon : (def != nullptr ? def->icon : std::string());
      if (!view.icon.empty() && !commands::isValidIconName(view.icon)) view.icon.clear();
      if (def != nullptr) {
        view.selectable = safeCall(def->enabled, true) && safeCall(def->visible, true);
        const bool stateful = def->kind == commands::CommandKind::Toggle || def->kind == commands::CommandKind::Radio;
        view.checked = stateful && safeCall(def->checked, false);
      }
    }
    views.push_back(std::move(view));
  }
  return views;
}

// ---- lifecycle ----------------------------------------------------------------------------------

void PieTrigger::onAttached() {
  core::layout::Style& s = style();
  s.direction = core::layout::FlexDirection::Column;
  s.alignItems = core::layout::Align::Stretch;
  s.width = core::layout::Length::percent(100.0);
  s.height = core::layout::Length::percent(100.0);
}

void PieTrigger::onDetached() {
  if (timer_ != 0) ui().cancelTimer(timer_);
  timer_ = 0;
  if (overlay_.valid()) {
    closingOverlay_ = true;
    ui().overlays().close(overlay_, DismissReason::Programmatic);
  }
  overlay_ = OverlayId{};
  pie_ = core::tree::WidgetId{};
  gesture_.cancel();
}

uint8_t PieTrigger::phases() const { return core::events::kListenCapture | core::events::kListenTarget | core::events::kListenBubble; }

// ---- gesture ------------------------------------------------------------------------------------

void PieTrigger::onPointerDown(Event& e) {
  if (e.phase == Phase::Bubble || e.button != Button::Right || !enabled()) return;
  if (!gesture_.active() && !swallowRelease_) swallowClick_ = false;  // a stale flag must not eat an ordinary right click
  if (gesture_.active() || swallowRelease_ || e.buttons != core::events::buttonBit(Button::Right)) return;
  if (!provider_) return;
  std::optional<commands::custommenu::CustomMenu> pie;
  try {
    pie = provider_(e.x, e.y);
  } catch (...) {
    return;  // a failing provider means "no pie here"
  }
  if (!pie || pie->kind != commands::custommenu::MenuKind::Pie || !commands::custommenu::isValidPieSlotCount(pie->slotCount) ||
      pie->entries.size() != static_cast<size_t>(pie->slotCount)) {
    return;
  }
  std::vector<PieSlotView> views = pieSlotViews(services_, *pie);
  std::vector<bool> selectable;
  selectable.reserve(views.size());
  for (const PieSlotView& v : views) selectable.push_back(v.filled && v.selectable);
  if (!gesture_.begin(e.x, e.y, ui().now(), std::move(selectable))) return;
  if (!ui().router().capturePointer(id())) {
    gesture_.cancel();
    return;
  }
  menu_ = std::move(*pie);
  views_ = std::move(views);
  swallowClick_ = true;
  startDrawTimer();
  e.markHandled();
  e.stopPropagation();
}

void PieTrigger::onPointerMove(Event& e) {
  if (e.phase == Phase::Bubble && e.target != id()) return;
  if (!gesture_.active()) return;
  if (gesture_.move(e.x, e.y)) updateHighlight();
  e.markHandled();
}

void PieTrigger::onDragStart(Event& e) {
  // The press that opened a pie is a gesture, not a drag of whatever lies under it: accepting the drag
  // also keeps the router from sending a Click for it.
  if (gesture_.active() || swallowRelease_) e.markHandled();
}

void PieTrigger::onClick(Event& e) {
  if (e.button != Button::Right || !swallowClick_ || e.phase == Phase::Bubble) return;
  swallowClick_ = false;
  e.markHandled();
  e.stopPropagation();
}

void PieTrigger::onPointerUp(Event& e) {
  if (e.button != Button::Right || (e.phase == Phase::Bubble && e.target != id())) return;
  if (swallowRelease_) {
    swallowRelease_ = false;
    e.markHandled();
    return;
  }
  if (!gesture_.active()) return;
  const PieOutcome outcome = gesture_.release(e.x, e.y, ui().now());
  commands::custommenu::CustomMenu menu = std::move(menu_);
  if (timer_ != 0) ui().cancelTimer(timer_);
  timer_ = 0;
  closePie();
  e.markHandled();

  // Everything below may destroy this widget: only locals are used.
  const CommandServices services = services_;
  const auto onExecuted = onExecuted_;
  const auto onFallback = onFallback_;
  const auto onCancelled = onCancelled_;
  const double x = e.x;
  const double y = e.y;
  switch (outcome.kind) {
    case PieOutcomeKind::Execute: {
      const std::string commandId = menu.entries[static_cast<size_t>(outcome.slot)].commandId;
      commands::ExecuteResult result;
      try {
        result = services.router.execute(commandId, commands::ExecuteSource::Menu);
      } catch (const std::exception& ex) {
        result = commands::ExecuteResult::refused(ex.what());
      } catch (...) {
        result = commands::ExecuteResult::refused("the command failed");
      }
      if (onExecuted) onExecuted(commandId, result);
      break;
    }
    case PieOutcomeKind::Fallback:
      if (onFallback) onFallback(x, y);
      break;
    case PieOutcomeKind::Cancel:
      if (onCancelled) onCancelled();
      break;
    case PieOutcomeKind::None: break;
  }
}

void PieTrigger::onCaptureLost(Event&) {
  swallowRelease_ = false;
  if (gesture_.active()) endGesture(true);
}

void PieTrigger::onKeyDown(Event& e) {
  if (e.key != Key::Escape || !gesture_.active()) return;
  cancelGesture();
  e.markHandled();
  e.stopPropagation();
}

void PieTrigger::cancelGesture() {
  if (!gesture_.active()) return;
  swallowRelease_ = true;
  endGesture(true);
}

void PieTrigger::endGesture(bool notifyCancelled) {
  if (timer_ != 0) ui().cancelTimer(timer_);
  timer_ = 0;
  gesture_.cancel();
  closePie();
  menu_ = commands::custommenu::CustomMenu{};
  views_.clear();
  if (notifyCancelled && onCancelled_) {
    const auto callback = onCancelled_;
    callback();
  }
}

// ---- drawing ------------------------------------------------------------------------------------

void PieTrigger::startDrawTimer() {
  UiContext* context = &ui();
  const core::tree::WidgetId self = id();
  const uint64_t delay = gesture_.msUntilDraw(ui().now());
  timer_ = ui().setTimer(delay, [context, self] {
    if (PieTrigger* trigger = context->objectAs<PieTrigger>(self)) {
      trigger->timer_ = 0;
      trigger->onDrawTimer();
    }
  });
}

void PieTrigger::onDrawTimer() {
  if (gesture_.tick(ui().now())) showPie();
}

void PieTrigger::showPie() {
  UiContext* context = &ui();
  const core::tree::WidgetId self = id();
  OverlayOptions options;
  const int half = static_cast<int>(PieMenu::extent() / 2.0);
  options.anchor = {static_cast<int32_t>(gesture_.originX()) - half, static_cast<int32_t>(gesture_.originY()) - half, 0, 0};
  options.placement = Placement::Manual;
  options.flip = false;
  options.maxHeightFraction = 0.0;
  options.windowMargin = 0.0;
  options.modal = false;
  options.dismissOnOutsidePress = false;
  options.dismissOnEscape = true;
  options.escapeFirst = true;
  options.dismissOnWindowDeactivate = true;
  options.restoreFocus = false;
  options.interactive = true;  // a non-interactive overlay is skipped by Escape; the host is made hit-transparent below
  options.surface = OverlaySurface::None;
  options.onClosed = [context, self](DismissReason reason) {
    if (PieTrigger* trigger = context->objectAs<PieTrigger>(self)) trigger->overlayClosed(reason);
  };
  OverlayHandle handle;
  try {
    handle = ui().overlays().open(options);
    if (!handle.valid()) return;
    overlay_ = handle.id;
    if (core::tree::Widget* host = ui().tree().get(handle.host)) host->flags.hitTestTransparent = true;
    PieMenu& pie = ui().create<PieMenu>(handle.host, views_, gesture_.config().deadZone);
    pie_ = pie.id();
    pie.setHighlight(gesture_.highlighted());
  } catch (const std::exception&) {
    closePie();  // the gesture goes on without a visible pie; the direction still selects
  }
}

void PieTrigger::overlayClosed(DismissReason) {
  if (closingOverlay_) return;
  overlay_ = OverlayId{};
  pie_ = core::tree::WidgetId{};
  if (!gesture_.active()) return;
  swallowRelease_ = true;  // the button is still down: its release must open nothing
  endGesture(true);
}

void PieTrigger::closePie() {
  if (overlay_.valid()) {
    closingOverlay_ = true;
    ui().overlays().close(overlay_, DismissReason::Programmatic);
    closingOverlay_ = false;
  }
  overlay_ = OverlayId{};
  pie_ = core::tree::WidgetId{};
}

void PieTrigger::updateHighlight() {
  if (!pie_.valid()) return;
  if (PieMenu* pie = ui().objectAs<PieMenu>(pie_)) pie->setHighlight(gesture_.highlighted());
}

}  // namespace r1ui::widgets
