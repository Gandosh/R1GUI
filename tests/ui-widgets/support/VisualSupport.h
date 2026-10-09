// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the one-line entry point of widget visual tests: R1_EXPECT_MATCHES(build, spec) renders the
//   widget the `build` function creates offscreen and compares it with a reference crop from
//   tests/reference/openpencil, under a tolerance profile, printing the metrics and leaving the
//   render (and a diff image on failure) under R1UI_ARTIFACT_DIR.
// Why: a widget's visual test should say what it is (widget, reference, state, theme, profile) and
//   nothing else; paths, rig creation, PNG output and reporting live here and in VisualHarness.
// Callers: tests/ui-widgets/<folder>/*_visual_test.cpp (label gpu; they link r1ui::widgets_gpu).
// Example:
//   r1test::visual::Build build = [](UiContext& ui, WidgetId parent) {
//     return ui.create<Label>(parent, "Layout", LabelRole::Heading).id();
//   };
//   R1_EXPECT_MATCHES(build, (VisualSpec{.reference = "widget-panel-section-title-idle", .profile = "text"}));
#pragma once

#include <cstdio>
#include <string>

#include "TestSupport.h"
#include "r1ui/widgets/testing/VisualHarness.h"

namespace r1test::visual {

using r1ui::widgets::testing::BuildFn;
using r1ui::widgets::testing::VisualSpec;
using r1ui::widgets::testing::VisualState;
using Build = BuildFn;

inline r1ui::widgets::testing::VisualPaths paths() { return {assetsDir(), referenceDir(), artifactDir()}; }

// Returns true when the render matches; prints one line either way and counts a failure.
inline bool expectMatches(const BuildFn& build, const VisualSpec& spec, const char* file, int line) {
  const auto result = r1ui::widgets::testing::compareWithReference(build, spec, paths());
  std::printf("visual %-44s %-5s %-6s %s\n", spec.reference.c_str(), spec.theme == r1ui::theme::ThemeId::Light ? "light" : "dark",
              spec.profile.c_str(), result.summary().c_str());
  if (!result.pass()) std::fprintf(stderr, "  render: %s\n  diff:   %s\n", result.renderPath.string().c_str(), result.diffPath.string().c_str());
  report(result.pass(), spec.reference.c_str(), file, line);
  return result.pass();
}

}  // namespace r1test::visual

#define R1_EXPECT_MATCHES(build, spec) ::r1test::visual::expectMatches((build), (spec), __FILE__, __LINE__)
