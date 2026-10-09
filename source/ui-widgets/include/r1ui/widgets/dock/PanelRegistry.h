// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PanelRegistry, the table of panels an application offers to the dock (id, title, icon,
//   kind and flags, minimum and default floating size, suggested first position) and the factory
//   that creates a panel's content widget.
// Why: the dock model only knows ids and flags; what a panel looks like and how it is made is the
//   application's business. The registry is the one place the application describes that, and the
//   DockHost reads it for tab titles and to create content lazily.
// Callers: the application (registers panels at startup, may add more later), DockHost (reads),
//   tests and the gallery. Calls: nothing (data and std::function).
// Content contract: the factory is called on the UI thread, at most once while the panel is open,
//   the first time the panel is mounted in a region body or floating window; it creates the content
//   widget as a child of `parent` and returns its id (an invalid id means "no content": the host
//   shows an empty body). The host keeps the widget alive while the panel stays open, moves it
//   between regions without recreating it and destroys it when the panel closes. The host forces the
//   content to fill its body (width and height 100%).
// Boundaries: ids are unique and non-zero; titles and icon names are cut to kMaxTitleBytes; at most
//   kMaxPanels entries; a rejected add leaves the registry unchanged.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/dock/DockTypes.h"

namespace r1ui::widgets {

class UiContext;

using PanelFactory = std::function<core::tree::WidgetId(UiContext& ui, core::tree::WidgetId parent)>;

struct PanelDescriptor {
  dock::PanelId id = 0;
  std::string title;
  std::string icon;  // icon name from assets/icons, or empty
  dock::PanelKind kind = dock::PanelKind::Panel;
  bool canClose = true;
  bool canFloat = true;
  bool locked = false;                 // default lock flag (a stored layout may override it)
  dock::PanelPlacement suggested;      // where the panel first appears in a layout that never saw it
  dock::Point minSize{0.0, 0.0};       // minimum content size; a floating window never gets smaller
  dock::Point floatSize{0.0, 0.0};     // default floating content size; 0 = the dock's default
  PanelFactory factory;
};

class PanelRegistry {
 public:
  static constexpr size_t kMaxTitleBytes = 256;

  // False (nothing changed) for a zero or duplicate id, a full registry or a non-finite size.
  bool add(PanelDescriptor descriptor);
  // False for an unknown id. Titles and icons are cut at a UTF-8 boundary.
  bool setTitle(dock::PanelId id, std::string title);
  bool setIcon(dock::PanelId id, std::string icon);
  const PanelDescriptor* find(dock::PanelId id) const;
  const std::vector<PanelDescriptor>& all() const { return panels_; }
  size_t size() const { return panels_.size(); }
  // The model's view of the panels, in registration order.
  std::vector<dock::PanelInfo> infos() const;

 private:
  std::vector<PanelDescriptor> panels_;
};

}  // namespace r1ui::widgets
