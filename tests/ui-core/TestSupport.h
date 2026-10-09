// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tiny assertion runner and tree/style builders shared by the ui-core tree, layout,
//   events and invalidation test programs.
// Why: the repo's tests are plain executables (exit code 0 = pass); named cases make a failure
//   point at the scenario it belongs to.
// Callers: tests/ui-core/{tree,layout,events,invalidation}_test.cpp only.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

#include "r1ui/core/layout/FlexLayout.h"
#include "r1ui/core/layout/Style.h"
#include "r1ui/core/tree/WidgetTree.h"

namespace core_test {

inline int& failures() {
  static int count = 0;
  return count;
}

inline int& checks() {
  static int count = 0;
  return count;
}

inline const char*& currentCase() {
  static const char* name = "";
  return name;
}

inline void expect(bool ok, const char* what) {
  ++checks();
  if (!ok) {
    std::fprintf(stderr, "FAIL [%s]: %s\n", currentCase(), what);
    ++failures();
  }
}

inline void expectRect(const r1ui::core::layout::Rect& got, int x, int y, int w, int h, const char* what) {
  ++checks();
  if (got.x != x || got.y != y || got.w != w || got.h != h) {
    std::fprintf(stderr, "FAIL [%s]: %s: got (%d,%d,%d,%d) expected (%d,%d,%d,%d)\n", currentCase(), what,
                 got.x, got.y, got.w, got.h, x, y, w, h);
    ++failures();
  }
}

inline void runCase(const char* name, void (*fn)()) {
  currentCase() = name;
  fn();
}

inline int finish(const char* suite) {
  std::fprintf(stderr, "%s: %d checks, %d failures\n", suite, checks(), failures());
  return failures() == 0 ? 0 : 1;
}

// ---- builders ----
using r1ui::core::layout::Length;
using r1ui::core::layout::Style;
using r1ui::core::tree::WidgetId;
using r1ui::core::tree::WidgetTree;

inline WidgetId addChild(WidgetTree& tree, WidgetId parent, const Style& style = Style{}) {
  const auto r = tree.create(parent);
  if (!r.ok()) {
    std::fprintf(stderr, "test setup: create failed: %s\n", r1ui::core::tree::describe(r.error));
    std::exit(2);
  }
  tree.get(r.id)->style = style;
  return r.id;
}

inline WidgetId addRoot(WidgetTree& tree, const Style& style = Style{}) {
  const auto r = tree.createRoot();
  if (!r.ok()) std::exit(2);
  tree.get(r.id)->style = style;
  return r.id;
}

inline Style sized(double w, double h) {
  Style s;
  s.width = Length::px(w);
  s.height = Length::px(h);
  return s;
}

inline const r1ui::core::layout::Rect& rectOf(const WidgetTree& tree, WidgetId id) {
  return tree.get(id)->rect;
}

inline const r1ui::core::layout::Rect& absOf(const WidgetTree& tree, WidgetId id) {
  return tree.get(id)->absRect;
}

}  // namespace core_test
