// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Dialog.h: the dialog widget tree, the result bookkeeping and closing.
// Invariants: the result callback runs at most once, from the overlay's close callback (so after
//   focus was restored); `chosenSet` is the only way a dialog counts as answered; a dialog whose
//   overlay is already gone ignores late actions.
// Callers: application code, tests.
#include "r1ui/widgets/dialog/Dialog.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "r1ui/widgets/dialog/DialogParts.h"
#include "r1ui/widgets/menu/MenuModel.h"
#include "r1ui/widgets/popover/OverlayWatch.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

using core::tree::WidgetId;

namespace {

constexpr size_t kMaxActions = 8;
constexpr size_t kMaxDialogTextBytes = 4096;
constexpr double kHeaderPadX = 16.0;
constexpr double kHeaderPadY = 12.0;
constexpr double kCloseButtonRoom = 24.0 + 8.0;   // close button plus a gap, kept free at the title's right
constexpr double kFooterGap = 8.0;

double sane(double v) { return std::isfinite(v) ? std::max(0.0, v) : 0.0; }

void setPadding(core::layout::Style& s, double left, double top, double right, double bottom) {
  s.padding[core::layout::kLeft] = left;
  s.padding[core::layout::kTop] = top;
  s.padding[core::layout::kRight] = right;
  s.padding[core::layout::kBottom] = bottom;
}

}  // namespace

struct DialogState {
  std::function<void(const DialogResult&)> onResult;
  std::string cancelId;
  std::string chosen;
  bool chosenSet = false;
  bool finished = false;
  OverlayId overlay;
};

namespace {

// Records the answer and closes the dialog; the result callback runs from the close callback.
bool choose(UiContext& ui, const std::shared_ptr<DialogState>& state, std::string action) {
  if (state->chosenSet || !ui.overlays().isOpen(state->overlay)) return false;
  state->chosen = std::move(action);
  state->chosenSet = true;
  ui.overlays().close(state->overlay, DismissReason::Programmatic);
  return true;
}

void finish(const std::shared_ptr<DialogState>& state) {
  if (state->finished) return;
  state->finished = true;
  DialogResult result;
  if (state->chosenSet) {
    result.action = state->chosen;
  } else {
    result.action = state->cancelId;
    result.dismissed = true;
  }
  const std::function<void(const DialogResult&)> callback = std::move(state->onResult);
  state->onResult = nullptr;
  if (callback) callback(result);
}

}  // namespace

DialogHandle openDialog(UiContext& ui, DialogSpec spec) {
  const auto state = std::make_shared<DialogState>();
  state->onResult = std::move(spec.onResult);
  if (spec.actions.size() > kMaxActions) spec.actions.resize(kMaxActions);
  for (DialogAction& a : spec.actions) {
    a.id = sanitizeUtf8(a.id, kMaxDialogTextBytes);
    a.label = sanitizeUtf8(a.label, kMaxDialogTextBytes);
    if (a.isCancel && state->cancelId.empty()) state->cancelId = a.id;
  }

  OverlayOptions oo;
  oo.surface = OverlaySurface::Dialog;
  oo.placement = Placement::Center;
  oo.modal = true;
  oo.scrim = true;
  oo.trapFocus = true;
  oo.dismissOnOutsidePress = spec.closeOnScrimPress;
  oo.dismissOnEscape = true;
  oo.focusOnOpen = false;   // the watch focuses the first body control, else the default action
  oo.restoreFocus = true;
  oo.maxHeightFraction = 0.8;
  oo.onClosed = [state](DismissReason) { finish(state); };
  const OverlayHandle handle = ui.overlays().open(oo);
  if (!handle.valid()) return {};
  state->overlay = handle.id;

  DialogHandle result;
  result.overlay = handle.id;
  result.host = handle.host;
  try {
    // Window-sized limits: never wider than the window.
    core::layout::Style& hostStyle = ui.object(handle.host)->style();
    const double room = std::max(1.0, ui.viewportWidth() - 8.0);
    const bool fixedWidth = spec.width > 0.0 && std::isfinite(spec.width);
    // A fixed width is the caller's choice; only content-sized dialogs are limited by maxWidth.
    const double maxWidth = fixedWidth ? room : std::min(std::isfinite(spec.maxWidth) && spec.maxWidth > 0.0 ? spec.maxWidth : 512.0, room);
    hostStyle.maxWidth = core::layout::Length::px(maxWidth);
    if (fixedWidth) hostStyle.width = core::layout::Length::px(std::min(spec.width, room));
    if (spec.height > 0.0 && std::isfinite(spec.height)) hostStyle.height = core::layout::Length::px(spec.height);

    const std::shared_ptr<std::string> defaultId = std::make_shared<std::string>();
    bool haveDefault = false;
    for (const DialogAction& a : spec.actions) {
      if (a.isDefault && a.enabled && !haveDefault) {
        *defaultId = a.id;
        haveDefault = true;
      }
    }
    UiContext* uiPtr = &ui;
    DialogContent& content = ui.create<DialogContent>(handle.host, [uiPtr, state, defaultId, haveDefault]() {
      if (haveDefault) choose(*uiPtr, state, *defaultId);
    });
    result.content = content.id();

    // Header: title, optional description, padded; the close button is absolute and created last so
    // it is the last stop of the Tab order.
    DialogBox& header = ui.create<DialogBox>(content.id());
    header.style().direction = core::layout::FlexDirection::Column;
    header.style().alignItems = core::layout::Align::Stretch;
    header.style().gapRow = 4.0;
    const bool hasHeader = !spec.title.empty() || !spec.description.empty();
    if (hasHeader) {
      // The divider is a 1 px border below the header: it takes a pixel of height like a CSS border.
      setPadding(header.style(), kHeaderPadX, kHeaderPadY, kHeaderPadX + (spec.closeButton ? kCloseButtonRoom : 0.0), kHeaderPadY + (spec.headerDivider ? 1.0 : 0.0));
      // The header text rows are 20 px (title) and 16 px (description); the close button is 24 px
      // high, so a title-only header is 24 px of content, like the measured variables dialog.
      if (!spec.title.empty()) {
        header.style().minHeight = core::layout::Length::px(24.0 + 2.0 * kHeaderPadY + (spec.headerDivider ? 1.0 : 0.0));
        header.style().justifyContent = core::layout::Justify::Center;
      }
      header.setDividerBelow(spec.headerDivider);
      if (!spec.title.empty()) ui.create<DialogText>(header.id(), sanitizeUtf8(spec.title, kMaxDialogTextBytes), "dialog.title");
      if (!spec.description.empty()) ui.create<DialogText>(header.id(), sanitizeUtf8(spec.description, kMaxDialogTextBytes), "dialog.description");
    } else {
      header.style().display = core::layout::Display::None;
    }

    DialogBox& body = ui.create<DialogBox>(content.id());
    body.style().direction = core::layout::FlexDirection::Column;
    body.style().alignItems = core::layout::Align::Stretch;
    body.style().flexGrow = 1.0;
    body.style().flexShrink = 1.0;
    body.style().minHeight = core::layout::Length::px(0);
    body.style().overflow = core::layout::Overflow::Hidden;
    const double pad = sane(spec.bodyPadding);
    setPadding(body.style(), pad, hasHeader ? 0.0 : pad, pad, spec.actions.empty() ? pad : 0.0);
    result.body = body.id();

    WidgetId firstButton;
    WidgetId defaultButton;
    if (!spec.actions.empty()) {
      DialogBox& footer = ui.create<DialogBox>(content.id());
      footer.style().direction = core::layout::FlexDirection::Row;
      footer.style().justifyContent = core::layout::Justify::End;
      footer.style().alignItems = core::layout::Align::Center;
      footer.style().gapColumn = kFooterGap;
      setPadding(footer.style(), kHeaderPadX, 12.0, kHeaderPadX, 16.0);
      footer.style().flexShrink = 0.0;
      for (const DialogAction& a : spec.actions) {
        DialogButton& button = ui.create<DialogButton>(footer.id(), a);
        const std::string id = a.id;
        button.setOnActivate([uiPtr, state, id]() { choose(*uiPtr, state, id); });
        if (!firstButton.valid()) firstButton = button.id();
        if (a.isDefault && a.enabled && !defaultButton.valid()) defaultButton = button.id();
      }
    }
    WidgetId closeButton;
    if (spec.closeButton) {
      DialogCloseButton& close = ui.create<DialogCloseButton>(content.id());
      close.setOnActivate([uiPtr, state]() {
        // Closing without an answer is a dismissal, like Escape.
        if (uiPtr->overlays().isOpen(state->overlay)) uiPtr->overlays().close(state->overlay, DismissReason::Programmatic);
      });
      closeButton = close.id();
    }
    OverlayWatch& watch = ui.create<OverlayWatch>(handle.host, handle.id, WidgetId{}, spec.owner);
    WidgetId fallback = defaultButton.valid() ? defaultButton : (firstButton.valid() ? firstButton : closeButton);
    watch.setInitialFocus(body.id(), fallback);
  } catch (const std::exception&) {
    ui.overlays().close(handle.id, DismissReason::Programmatic);
    return {};
  }
  result.state = state;
  return result;
}

bool closeDialog(UiContext& ui, const DialogHandle& handle, std::string action) {
  if (!handle.valid() || !handle.state) return false;
  return choose(ui, handle.state, std::move(action));
}

bool isDialogOpen(UiContext& ui, const DialogHandle& handle) { return handle.valid() && ui.overlays().isOpen(handle.overlay); }

}  // namespace r1ui::widgets
