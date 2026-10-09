// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Popover.h.
// Callers: application code, tests, the gallery.
#include "r1ui/widgets/popover/Popover.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/overlay/OverlayHost.h"
#include "r1ui/widgets/popover/OverlayWatch.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

double sane(double v) { return std::isfinite(v) ? std::max(0.0, v) : 0.0; }

}  // namespace

PopoverHandle openPopover(UiContext& ui, const PopoverOptions& options) {
  OverlayOptions oo;
  const bool followed = options.anchorWidget.valid() && ui.alive(options.anchorWidget);
  oo.anchor = followed ? ui.absRect(options.anchorWidget) : options.anchorRect;
  oo.placement = options.placement;
  oo.gap = sane(options.gap);
  oo.flip = options.flip;
  oo.matchAnchorWidth = options.matchAnchorWidth;
  if (followed) oo.anchorWidget = options.anchorWidget;
  oo.modal = options.modal;
  oo.dismissOnOutsidePress = options.dismissOnOutsidePress;
  oo.dismissOnEscape = options.dismissOnEscape;
  oo.dismissOnWindowDeactivate = options.dismissOnWindowDeactivate;
  oo.focusOnOpen = options.focusOnOpen;
  oo.restoreFocus = options.restoreFocus;
  oo.surface = OverlaySurface::Popover;
  oo.onClosed = options.onClosed;
  const OverlayHandle handle = ui.overlays().open(oo);
  if (!handle.valid()) return {};
  if (WidgetObject* host = ui.object(handle.host)) {
    core::layout::Style& s = host->style();
    for (double& p : s.padding) p += sane(options.padding);  // on top of the host border
    if (options.width > 0.0 && std::isfinite(options.width)) s.width = core::layout::Length::px(options.width);
    host->requestLayout();
  }
  try {
    ui.create<OverlayWatch>(handle.host, handle.id, followed ? options.anchorWidget : core::tree::WidgetId{}, options.owner);
  } catch (const std::exception&) {
    ui.overlays().close(handle.id, DismissReason::Programmatic);
    return {};
  }
  return {handle.id, handle.host};
}

bool closePopover(UiContext& ui, const PopoverHandle& handle) { return handle.valid() && ui.overlays().close(handle.overlay, DismissReason::Programmatic); }

bool isPopoverOpen(UiContext& ui, const PopoverHandle& handle) { return handle.valid() && ui.overlays().isOpen(handle.overlay); }

}  // namespace r1ui::widgets
