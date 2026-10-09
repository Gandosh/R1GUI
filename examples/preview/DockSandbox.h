// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: preview mode 3, the docking sandbox: an interactive demonstration of the docking specs
//   (02, 04, 05, 08) on top of the headless ui-dock model. It owns the pointer state machine
//   (tab press/drag/drop, splitter drag, middle-click close), layout save/load/reset to a file,
//   and the drawing of the dock through the Painter.
// Why: the owner needs something to click through; all dock decisions stay in ui-dock, this file
//   only translates events into model calls and model geometry into rectangles.
// Callers: examples/preview main.cpp (Viewer), which forwards key and pointer events while the
//   mode is active. Calls: r1ui::dock::DockLayout, r1ui::theme::Tokens, r1ui::platform::Window.
// Look: tabs are coloured rectangles (the front tab brighter and taller) labelled with the panel
//   name, the ghost is an opaque tab-sized swatch, the drop preview is a four-rectangle outline.
//   Results are shown in a status line at the bottom of the area and in the window title. Sizes are
//   physical pixels without display scaling, as in the earlier preview.
// Failure behavior: a failed model call or an unreadable layout file only changes the status
//   text; the current layout is never replaced by invalid data.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "r1ui/dock/DockLayout.h"
#include "TextEngine.h"
#include "r1ui/platform/Window.h"
#include "r1ui/render/Painter.h"
#include "r1ui/theme/Tokens.h"

class DockSandbox {
 public:
  // Throws std::runtime_error if the tokens lack a colour the sandbox needs.
  DockSandbox(r1ui::platform::Window& window, const r1ui::theme::Tokens& tokens, std::filesystem::path layoutFile);

  // Handles S, L, R and Escape; other keys are ignored. Returns true when the key was used
  // (Escape is used only while a tab drag is active).
  bool onKey(uint32_t virtualKey);
  void onMouse(const r1ui::platform::MouseEvent& event);
  // The area (physical pixels, window coordinates) the dock and its status line occupy.
  void setBounds(const r1ui::render::Rect& bounds) { bounds_ = bounds; }
  // Draws the dock and the status line into the bounds.
  void draw(r1ui::render::Painter& painter, preview::TextEngine& text, r1ui::theme::ThemeId theme);
  std::string title() const;
  const std::string& status() const { return status_; }
  bool dragActive() const { return state_ == State::TabDragging; }

 private:
  enum class State { Idle, TabPressed, TabDragging, SplitterDragging, MiddlePressed };

  r1ui::dock::Rect mainRect() const;
  void setStatus(std::string text);
  void resetLayout();
  void saveLayout();
  void loadLayout();
  void finishTabDrag(const r1ui::dock::DropZone& zone);
  void onMove(r1ui::dock::Point p);
  std::string describe(const r1ui::dock::DropZone& zone, r1ui::dock::PanelId panel) const;
  std::string nameOf(r1ui::dock::PanelId panel) const;

  r1ui::platform::Window& window_;
  const r1ui::theme::Tokens& tokens_;
  std::filesystem::path file_;
  r1ui::dock::DockLayout layout_;
  std::string status_;
  r1ui::render::Rect bounds_;

  State state_ = State::Idle;
  r1ui::dock::PanelId panel_ = 0;     // tab being pressed / dragged / middle-clicked
  r1ui::dock::Point press_;           // pointer at press
  r1ui::dock::Point grab_;            // pointer minus the pressed tab's top-left
  r1ui::dock::Point pointer_;         // last pointer position
  r1ui::dock::SplitterHandle handle_; // splitter being dragged
  double handleGrab_ = 0.0;           // pointer minus handle centre along its axis at press
  r1ui::dock::DropZone zone_;         // current drop zone while dragging
  bool hoverHandle_ = false;
};
