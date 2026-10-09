// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tiny document model of the composed screen: one rectangle with the properties the
//   properties panel edits (position, size, rotation, opacity, radius, corner smoothing, fill, clip)
//   and a name, plus the selection flag.
// Why: the screen composes widgets of five groups; a shared model is what makes them one app: the
//   panel's fields write it, the canvas paints it, the layer tree names it. It is data only: the
//   app (ComposedApp) refreshes the views after a change.
// Callers: ComposedApp*.cpp, CanvasView.cpp, tests/preview.
#pragma once

#include <string>

#include "r1ui/widgets/colorpicker/ColorModel.h"

namespace preview {

struct RectangleProps {
  double x = 141.0;
  double y = 200.0;
  double width = 300.0;
  double height = 220.0;
  double rotation = 0.0;
  double opacity = 100.0;  // percent
  double radius = 0.0;
  double smoothing = 0.0;  // percent
  r1ui::widgets::color::Rgba fill{{0xD4 / 255.0, 0xD4 / 255.0, 0xD4 / 255.0}, 1.0};
  bool clipContent = false;
};

struct AppDocument {
  RectangleProps rect;
  std::string name = "Rectangle 1";
  bool selected = true;
};

}  // namespace preview
