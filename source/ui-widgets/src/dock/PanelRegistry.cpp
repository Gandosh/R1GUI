// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the implementation of PanelRegistry (see PanelRegistry.h).
// Invariants: ids are unique; text is cut on UTF-8 boundaries by sanitizeUtf8 (invalid sequences
//   become U+FFFD), so the dock never draws or stores a malformed title.
#include "r1ui/widgets/dock/PanelRegistry.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/menu/MenuModel.h"

namespace r1ui::widgets {

namespace {

bool sizeOk(const dock::Point& p) { return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0.0 && p.y >= 0.0 && p.x <= dock::kMaxCoordinate && p.y <= dock::kMaxCoordinate; }

}  // namespace

bool PanelRegistry::add(PanelDescriptor d) {
  if (d.id == 0 || panels_.size() >= dock::kMaxPanels || find(d.id) != nullptr) return false;
  if (!sizeOk(d.minSize) || !sizeOk(d.floatSize)) return false;
  d.title = sanitizeUtf8(d.title, kMaxTitleBytes);
  d.icon = sanitizeUtf8(d.icon, kMaxTitleBytes);
  panels_.push_back(std::move(d));
  ++revision_;
  return true;
}

bool PanelRegistry::setTitle(dock::PanelId id, std::string title) {
  for (PanelDescriptor& p : panels_) {
    if (p.id == id) {
      p.title = sanitizeUtf8(title, kMaxTitleBytes);
      ++revision_;
      return true;
    }
  }
  return false;
}

bool PanelRegistry::setIcon(dock::PanelId id, std::string icon) {
  for (PanelDescriptor& p : panels_) {
    if (p.id == id) {
      p.icon = sanitizeUtf8(icon, kMaxTitleBytes);
      ++revision_;
      return true;
    }
  }
  return false;
}

const PanelDescriptor* PanelRegistry::find(dock::PanelId id) const {
  for (const PanelDescriptor& p : panels_) {
    if (p.id == id) return &p;
  }
  return nullptr;
}

std::vector<dock::PanelInfo> PanelRegistry::infos() const {
  std::vector<dock::PanelInfo> out;
  out.reserve(panels_.size());
  for (const PanelDescriptor& p : panels_) {
    dock::PanelInfo info;
    info.id = p.id;
    info.title = p.title;
    info.canClose = p.canClose;
    info.kind = p.kind;
    info.canFloat = p.canFloat;
    info.locked = p.locked;
    info.suggested = p.suggested;
    info.floatWidth = p.floatSize.x;
    info.floatHeight = p.floatSize.y;
    out.push_back(std::move(info));
  }
  return out;
}

}  // namespace r1ui::widgets
