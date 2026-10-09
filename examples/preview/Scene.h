// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the preview shell's own small widget tree: the root, the 32 px title bar (application and
//   mode names, the mode indicator squares, minimise / maximise / close) and the body area below it.
//   It has its own flexbox layout, pointer routing (Router), invalidation (Invalidator), hover/active
//   state, the painting of its tree through the Painter, and it reports the title-bar regions for the
//   platform's chrome hit test and the rectangle left for the modes.
// Why: the borderless window needs caption buttons whose behaviour (hover, OS hit testing) the
//   Phase 3 stack already provides; everything below the title bar is the widget library in a
//   UiContext (WidgetMode.h) or one of the direct-drawn modes.
// Callers: PreviewApp (event loop, frame), Bench, tests/preview (headless layout checks). Calls:
//   ui-core (WidgetTree, FlexLayout, Router, Invalidator), ui-theme (Theme, StyleSheet), ui-text via
//   TextEngine, ui-render Painter via the paint functions.
// Units: the tree and all layout values are logical pixels; the viewport is given in physical
//   pixels with the display scale and every drawing call multiplies by that scale.
// Threading: UI thread only. Invariants: nodes_[widget.userData] is the payload of every widget;
//   the tree is built once and never changes structure; ids are re-validated by the toolkit.
// Layout lifecycle: setViewport / setMode / text changes request layout through the Invalidator;
//   layout() runs it and must be called before paint() whenever needsFrame() is true. The theme is
//   the shared one (Services), so a switch repaints every UiContext as well.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Toolkit.h"
#include "UiNode.h"
#include "r1ui/core/events/EventHandler.h"
#include "r1ui/core/events/Router.h"
#include "r1ui/core/invalidation/Invalidator.h"
#include "r1ui/core/layout/Measure.h"
#include "r1ui/core/tree/WidgetTree.h"
#include "r1ui/platform/Window.h"
#include "r1ui/render/Painter.h"
#include "r1ui/theme/StyleSheet.h"
#include "r1ui/theme/Theme.h"

namespace preview {

inline constexpr double kTitleBarHeight = 32.0;

// Where in the frame the time went, filled by Scene::layout (milliseconds, wall clock).
struct LayoutResult {
  bool ran = false;
  size_t nodesCommitted = 0;
};

class Scene final : public r1ui::core::layout::MeasureProvider {
 public:
  // Throws std::runtime_error when the tokens lack something the scene needs (message names it).
  // `theme` is the shared theme and must outlive the scene.
  Scene(r1ui::theme::Theme& theme, TextEngine& text);
  ~Scene() override;
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;

  // ---- state ----
  r1ui::theme::Theme& theme() { return theme_; }
  const r1ui::theme::Theme& theme() const { return theme_; }
  void toggleTheme();
  void setMode(Mode mode);
  Mode mode() const { return mode_; }
  // Physical client size and display scale (1.0 = 96 dpi). Zero sizes (minimised) are accepted.
  void setViewport(int width, int height, float scale);
  void setMaximized(bool maximized);
  float scale() const { return scale_; }

  // ---- input ----
  // Translates one platform event for the router; `nowMs` is a monotonic clock.
  void handleEvent(const r1ui::platform::Event& event, uint64_t nowMs);
  void setGlobalKeyHandler(r1ui::core::events::GlobalKeyHandler* handler);
  r1ui::core::events::Router& router() { return router_; }
  r1ui::platform::CursorShape cursor() const { return cursor_; }

  // ---- frame ----
  bool needsFrame() const;
  // Discards every cached layout result; the next layout() recomputes the whole tree (benchmarks).
  void requestFullLayout() { invalidator_.requestFullLayout(); }
  LayoutResult layout();
  // Draws the whole tree. Call between beginFrame and endFrame of the target.
  void paint(r1ui::render::Painter& painter);

  // ---- results of the last layout ----
  r1ui::platform::ChromeLayout chromeLayout() const;
  // The area below the title bar, physical pixels (what the modes draw into).
  r1ui::render::Rect bodyRect() const;
  // Absolute logical rectangle of a widget, or empty for a stale id (tests, chrome, bench).
  r1ui::core::layout::Rect absRect(r1ui::core::tree::WidgetId id) const;
  const r1ui::core::tree::WidgetTree& tree() const { return tree_; }
  // Ids of notable widgets (tests and chrome registration).
  struct Ids {
    r1ui::core::tree::WidgetId root, titleBar, minimize, maximize, close, body;
  };
  const Ids& ids() const { return ids_; }
  size_t widgetCount() const { return tree_.nodeCount(); }
  // The state bits of a mode indicator square (tests).
  uint8_t squareState(int index) const;

  // MeasureProvider: text widgets measure with the text module (logical pixels).
  r1ui::core::layout::MeasureResult measure(r1ui::core::tree::WidgetId widget,
                                            const r1ui::core::layout::MeasureInput& input) override;

 private:
  friend class SceneBuilder;
  class Handler;

  using WidgetId = r1ui::core::tree::WidgetId;

  Node* nodeOf(WidgetId id);
  const Node* nodeOf(WidgetId id) const;
  const r1ui::theme::ResolvedStyle& resolved(CachedStyle& cache, const std::string& key, uint8_t state);
  void setStateBit(WidgetId id, uint8_t bit, bool on);
  void onWidgetEvent(r1ui::core::events::Event& event);
  void updateCursor();
  void paintWidget(r1ui::render::Painter& painter, WidgetId id);
  void paintNode(r1ui::render::Painter& painter, WidgetId id, Node& node, const r1ui::render::Rect& rect);
  void paintBox(r1ui::render::Painter& painter, Node& node, const r1ui::render::Rect& rect);
  void paintChromeGlyph(r1ui::render::Painter& painter, const Node& node, const r1ui::render::Rect& rect,
                        const r1ui::render::Color& color);
  r1ui::render::Rect physical(const r1ui::core::layout::Rect& rect) const;
  r1ui::render::Color color(const r1ui::theme::Color& c, double opacity = 1.0) const;
  r1ui::render::Color themeColor(std::string_view name) const;

  r1ui::theme::Theme& theme_;
  TextEngine& text_;
  r1ui::theme::StyleSheet sheet_;
  r1ui::core::tree::WidgetTree tree_;
  r1ui::core::events::Router router_;
  r1ui::core::invalidation::Invalidator invalidator_;
  std::unique_ptr<r1ui::core::events::EventHandler> handler_;
  std::vector<Node> nodes_;
  Ids ids_;
  WidgetId modeText_;
  std::array<WidgetId, kModeCount> squares_;

  Mode mode_ = Mode::Gallery;
  float scale_ = 1.0f;
  int viewportWidth_ = 0;
  int viewportHeight_ = 0;
  r1ui::platform::CursorShape cursor_ = r1ui::platform::CursorShape::Arrow;
};

}  // namespace preview
