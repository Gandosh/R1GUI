// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the visual test harness: render a widget (or a small widget tree) offscreen at a given size,
//   theme, state and scale into an RGBA image, write it as PNG, and compare it with a reference PNG
//   crop from tests/reference through the in-process port of imgdiff.py.
// Why: widget tests must be one-liners ("this widget in this state matches that reference crop under
//   that tolerance profile") that run in CTest, give numbers when they fail and leave the render and
//   a diff image behind for the eye.
// Callers: tests/ui-widgets/<folder>/*_visual_test.cpp (through tests/ui-widgets/support/VisualSupport.h
//   which fills in the directories), calibration tools. Calls: UiContext, OffscreenTarget, ImageDiff.
// Rig: one RenderDevice and one Services (text engine, icon cache) are created on first use and
//   shared by every render of the process; the theme of the services is set per render. The GPU is
//   chosen like everywhere else (R1UI_GPU). Renders are deterministic: animations are off, the
//   pointer is placed by synthetic input, the clock is fixed.
// Scene: the surface behind the widget is cleared with the theme token `background` (default
//   "panel"). The widget is built as a child of the root, which has `padding` logical px on every
//   side (6 like the reference crops) and top-left alignment, so it takes its natural size unless the
//   builder sets one. State: Hover moves the pointer to the widget's centre; Pressed also presses
//   the left button; Focus gives it keyboard focus (focus ring visible); Disabled disables it.
// Failure behavior: GPU or asset failures throw; a missing reference image or a size mismatch is
//   reported in the result (never thrown) so one failing test prints its numbers.
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/theme/Tokens.h"
#include "r1ui/widgets/image/ImageDiff.h"
#include "r1ui/widgets/image/Png.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets::testing {

enum class VisualState { Idle, Hover, Focus, Pressed, Disabled };

const char* visualStateName(VisualState state);

// Adds the widget(s) under `parent` and returns the id of the one the state applies to.
using BuildFn = std::function<core::tree::WidgetId(UiContext& ui, core::tree::WidgetId parent)>;

struct RenderSpec {
  int width = 0;   // logical pixels of the whole image
  int height = 0;
  theme::ThemeId theme = theme::ThemeId::Dark;
  VisualState state = VisualState::Idle;
  float scale = 1.0f;
  int padding = 6;
  std::string background = "panel";
};

struct VisualPaths {
  std::filesystem::path assetsDir;     // contains fonts/, icons/, theme/tokens.json
  std::filesystem::path referenceDir;  // tests/reference
  std::filesystem::path artifactDir;   // where renders and diffs are written
};

// Renders offscreen. Throws std::invalid_argument for a zero size. The first call fixes the assets
// directory of the shared rig.
image::Image renderWidget(const BuildFn& build, const RenderSpec& spec, const VisualPaths& paths);

struct VisualSpec {
  std::string reference;               // file name without ".png", e.g. "widget-panel-section-title-idle"
  theme::ThemeId theme = theme::ThemeId::Dark;
  VisualState state = VisualState::Idle;
  std::string profile = "default";     // tolerance.json profile: flat, text, icons, screen, default
  float scale = 1.0f;
  int padding = 6;
  std::string background = "panel";
  // Pixels of the reference that are ignored (painted over with the render in both images), for
  // known deviations such as a neighbouring widget in the crop: {x, y, w, h} in image pixels.
  struct Ignore {
    int x = 0, y = 0, w = 0, h = 0;
  };
  std::vector<Ignore> ignore;
};

struct VisualResult {
  image::DiffMetrics metrics;
  std::string error;               // reference could not be read
  std::filesystem::path renderPath;
  std::filesystem::path diffPath;
  bool pass() const { return error.empty() && metrics.error.empty() && metrics.pass; }
  std::string summary() const;
};

// Renders `build` at the size of the reference crop and compares. Always writes
// <artifactDir>/<reference>[-light].png and, when pixels fail, <...>.diff.png.
VisualResult compareWithReference(const BuildFn& build, const VisualSpec& spec, const VisualPaths& paths);

// Number of validation-layer messages seen by the shared device (Debug trees); 0 otherwise.
unsigned validationMessageCount();

// Sum of the ink in a region: for each pixel the distance of its colour from `background`
// projected on (foreground - background), clamped to 0..1 per pixel, summed. Used to compare text
// weight between renders and references independently of antialiasing details.
double inkSum(const image::Image& image, int x, int y, int w, int h, const theme::Color& background, const theme::Color& foreground);

}  // namespace r1ui::widgets::testing
