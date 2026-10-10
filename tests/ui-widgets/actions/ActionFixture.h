// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the fixture of the action list tests: a generator of sample actions (several categories, every
//   fifth with a shortcut), a headless window with an ActionList of fixed size, and the pointer / key
//   helpers the tests share.
// Why: the list tests (structure, search, drag, speed, hostile input) all start from the same scene;
//   keeping it here keeps each test a page of expectations.
// Callers: tests/ui-widgets/actions/*_test.cpp.
#pragma once

#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/widgets/actions/ActionList.h"

namespace r1test {

using r1ui::core::events::Key;
using r1ui::core::layout::RectD;
using r1ui::core::tree::WidgetId;
using r1ui::widgets::ActionInfo;
using r1ui::widgets::ActionList;
using r1ui::widgets::ActionListOptions;
using r1ui::widgets::ActionListView;

// `count` actions spread over the categories "Alpha", "Beta", "Gamma" (index mod 3); the third category
// is declared first so the input order differs from the sorted order.
inline std::vector<ActionInfo> sampleActions(size_t count) {
  static const char* kCategories[] = {"Gamma", "Alpha", "Beta"};
  std::vector<ActionInfo> out;
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    ActionInfo a;
    a.id = "cmd." + std::to_string(i);
    a.label = "Action " + std::to_string(i);
    a.description = "Does thing " + std::to_string(i) + " with the widget";
    a.category = kCategories[i % 3];
    a.icon = "circle";
    if (i % 5 == 0) a.shortcut = "Ctrl+" + std::string(1, static_cast<char>('A' + i % 26));
    out.push_back(std::move(a));
  }
  return out;
}

struct ActionScene {
  explicit ActionScene(std::vector<ActionInfo> actions, ActionListOptions options = {}, int width = 900, int height = 700) : t(width, height) {
    list = &t.ui.create<ActionList>(t.ui.root(), std::move(options));
    list->style().width = r1ui::core::layout::Length::px(700);
    list->style().height = r1ui::core::layout::Length::px(420);
    list->style().flexShrink = 0.0;
    list->setActions(std::move(actions));
    t.layout();
  }
  ActionListView& view() { return list->view(); }
  RectD rowRect(int row) { return view().rowRect(row); }
  int rowOf(const std::string& id) {
    const auto& rows = view().rows();
    for (size_t i = 0; i < rows.size(); ++i) {
      if (!rows[i].header && view().actions()[rows[i].index].id == id) return static_cast<int>(i);
    }
    return -1;
  }
  int headerOf(const std::string& category) {
    const auto& rows = view().rows();
    for (size_t i = 0; i < rows.size(); ++i) {
      if (rows[i].header && view().categories()[rows[i].index] == category) return static_cast<int>(i);
    }
    return -1;
  }
  std::vector<std::string> ids() {
    std::vector<std::string> out;
    for (const auto& r : view().rows()) {
      if (!r.header) out.push_back(view().actions()[r.index].id);
    }
    return out;
  }
  std::vector<std::string> headers() {
    std::vector<std::string> out;
    for (const auto& r : view().rows()) {
      if (r.header) out.push_back(view().categories()[r.index]);
    }
    return out;
  }
  void click(int row, double dx = 60.0) {
    const RectD r = rowRect(row);
    t.ui.pointerMove(r.x + dx, r.y + r.h / 2);
    t.ui.pointerDown(r.x + dx, r.y + r.h / 2);
    t.ui.pointerUp(r.x + dx, r.y + r.h / 2);
    t.layout();
  }
  bool key(Key k, uint8_t mods = 0) {
    const bool used = t.ui.keyDown(k, mods);
    t.layout();
    return used;
  }
  void focusView() { t.ui.focusWidget(list->viewWidget(), r1ui::core::events::FocusReason::Keyboard); }
  void paintOnce() {
    r1ui::render::Painter painter;
    painter.begin(900, 700);
    t.ui.paint(painter);
    painter.end();
  }

  TestUi t;
  ActionList* list = nullptr;
};

}  // namespace r1test
