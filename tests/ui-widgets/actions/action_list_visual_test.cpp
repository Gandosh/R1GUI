// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the GPU render of the action list in both themes, written under R1UI_ARTIFACT_DIR for the eye:
//   the idle list (groups, icons, descriptions, shortcuts, column headers), a search with highlighted
//   matches, a collapsed group with a selected row, and the gallery page with its drop zone while a row
//   is being dragged over it. Checks that the renders are not empty, differ between themes and states,
//   and that the validation layer (Debug trees) reports nothing.
// Callers: CTest (label gpu, offscreen, no window).
#include "VisualSupport.h"
#include "r1ui/widgets/actions/ActionList.h"
#include "r1ui/widgets/hotkeys/GalleryHotkeys.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;
using r1ui::core::tree::WidgetId;

// A column of the picture's size the page is built into (the harness wraps its content otherwise).
class Frame final : public WidgetObject {
 public:
  const char* typeName() const override { return "Frame"; }
  void onAttached() override {
    style().direction = r1ui::core::layout::FlexDirection::Column;
    style().alignItems = r1ui::core::layout::Align::Stretch;
    style().width = r1ui::core::layout::Length::px(1076);
    style().height = r1ui::core::layout::Length::px(536);
  }
};

std::vector<ActionInfo> sample() {
  struct Row {
    const char* id;
    const char* label;
    const char* description;
    const char* category;
    const char* icon;
    const char* shortcut;
  };
  const Row rows[] = {
      {"file.new", "New document", "Create an empty document in a new tab.", "File", "file", "Ctrl+N"},
      {"file.open", "Open...", "Choose a document on disk and open it.", "File", "folder-open", "Ctrl+O"},
      {"file.save", "Save", "Write the document to its file.", "File", "save", "Ctrl+S"},
      {"file.export", "Export as image", "Write the visible area as a PNG file; hold Shift to include the background and every hidden layer.", "File", "download", ""},
      {"edit.undo", "Undo", "Take back the last change.", "Edit", "undo2", "Ctrl+Z"},
      {"edit.redo", "Redo", "Do again the change that was taken back.", "Edit", "rotate-cw", "Ctrl+Shift+Z, Ctrl+Y"},
      {"edit.copy", "Copy", "Copy the selection to the clipboard.", "Edit", "copy", "Ctrl+C"},
      {"edit.paste", "Paste", "Insert the clipboard at the cursor.", "Edit", "clipboard", "Ctrl+V"},
      {"edit.delete", "Delete", "Remove the selection.", "Edit", "trash-2", "Delete, Backspace"},
      {"view.grid", "Show grid", "Toggle the grid lines behind the content.", "View", "grid-3x3", "Ctrl+G"},
      {"view.fit", "Fit to window", "Zoom so the whole document is visible.", "View", "maximize", "Ctrl+0"},
      {"help.about", "About", "Show the program version and credits.", "Help", "circle", ""},
  };
  std::vector<ActionInfo> out;
  for (const Row& r : rows) {
    ActionInfo a;
    a.id = r.id;
    a.label = r.label;
    a.description = r.description;
    a.category = r.category;
    a.icon = r.icon;
    a.shortcut = r.shortcut;
    out.push_back(std::move(a));
  }
  out.back().enabled = false;
  return out;
}

enum class State { Idle, Search, Collapsed, Page };

BuildFn pageIn(State state) {
  return [state](UiContext& ui, WidgetId parent) {
    if (state == State::Page) {
      Frame& frame = ui.create<Frame>(parent);
      buildGalleryActions(ui, frame.id());
      ui.frame();
      return parent;
    }
    ActionListOptions options;
    options.columnHeaders = true;
    ActionList& list = ui.create<ActionList>(parent, options);
    list.style().width = r1ui::core::layout::Length::px(860);
    list.style().height = r1ui::core::layout::Length::px(540);
    list.setActions(sample());
    ui.frame();
    if (state == State::Search) {
      list.setFilter("clip");
    } else if (state == State::Collapsed) {
      list.view().setCollapsed("File", true);
      list.view().selectAction("edit.redo");
    } else {
      list.view().selectAction("edit.copy");
    }
    ui.frame();
    return parent;
  };
}

}  // namespace

int main() {
  const VisualPaths paths = r1test::visual::paths();
  const char* names[] = {"idle", "search", "collapsed", "page"};
  image::Image first[4];
  for (const r1ui::theme::ThemeId theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    for (int s = 0; s < 4; ++s) {
      RenderSpec spec;
      spec.width = s == 3 ? 1100 : 900;
      spec.height = s == 3 ? 560 : 580;
      spec.theme = theme;
      spec.padding = 12;
      spec.background = "panel";
      const image::Image render = renderWidget(pageIn(static_cast<State>(s)), spec, paths);
      const bool dark = theme == r1ui::theme::ThemeId::Dark;
      image::writePng(paths.artifactDir / (std::string("action-list-") + names[s] + (dark ? "-dark" : "-light") + ".png"), render.width, render.height, render.rgba);
      R1_EXPECT(render.width == static_cast<uint32_t>(spec.width) && render.height == static_cast<uint32_t>(spec.height));
      if (dark) {
        first[s] = render;
      } else {
        R1_EXPECT(render.rgba != first[s].rgba);  // the themes differ
      }
    }
  }
  R1_EXPECT(first[0].rgba != first[1].rgba && first[1].rgba != first[2].rgba && first[0].rgba != first[2].rgba);
  R1_EXPECT(validationMessageCount() == 0);
  return r1test::finish();
}
