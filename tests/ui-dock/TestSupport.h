// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tiny assertion/case runner and layout builders shared by the ui-dock test programs.
// Why: the repo's tests are plain executables (exit code 0 = pass); named cases let a failure
//   point at the acceptance scenario it belongs to (docking_scenario_03 etc.).
// Callers: tests/ui-dock/*.cpp only.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "r1ui/dock/DockLayout.h"

namespace dock_test {

inline int& failures() {
  static int count = 0;
  return count;
}

inline const char*& currentCase() {
  static const char* name = "";
  return name;
}

inline void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL [%s]: %s\n", currentCase(), what);
    ++failures();
  }
}

inline bool near(double a, double b, double tolerance = 1.0) { return std::fabs(a - b) <= tolerance; }

inline void runCase(const char* name, void (*fn)()) {
  std::fprintf(stderr, "case %s\n", name);
  currentCase() = name;
  fn();
}

inline int finish(const char* program) {
  if (failures() == 0) std::printf("%s: all checks passed\n", program);
  return failures() == 0 ? 0 : 1;
}

using namespace r1ui::dock;

// Panels 1..count named "P<id>"; all closable unless listed in `pinned`.
inline std::vector<PanelInfo> makePanels(PanelId count, std::vector<PanelId> pinned = {}) {
  std::vector<PanelInfo> panels;
  for (PanelId id = 1; id <= count; ++id) {
    bool canClose = true;
    for (PanelId p : pinned) canClose = canClose && p != id;
    panels.push_back({id, "P" + std::to_string(id), canClose});
  }
  return panels;
}

inline DockLayout build(PanelId panelCount, Node root, const DockConfig& config = {}) {
  DockLayoutResult r = DockLayout::create(makePanels(panelCount), config, std::move(root));
  if (!r.ok()) {
    std::fprintf(stderr, "test setup failed: %s\n", r.error.c_str());
    std::abort();
  }
  return std::move(*r.layout);
}

inline const StackLayout* stackOf(const LayoutResult& layout, PanelId panel) {
  for (const StackLayout& s : layout.stacks) {
    for (const TabLayout& t : s.tabs) {
      if (t.panel == panel) return &s;
    }
  }
  return nullptr;
}

inline PanelId frontOf(const LayoutResult& layout, PanelId inStackOf) {
  const StackLayout* s = stackOf(layout, inStackOf);
  if (s == nullptr) return 0;
  for (const TabLayout& t : s->tabs) {
    if (t.active) return t.panel;
  }
  return 0;
}

}  // namespace dock_test
