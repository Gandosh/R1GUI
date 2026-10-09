// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the preview's widget scene: the retained widget tree (title bar, the Phase 3 properties
//   panel, the canvas area), its flexbox layout, pointer/keyboard routing (Router), invalidation
//   (Invalidator), hover/active/focus state, the theme switch and the painting of the tree through
//   the Painter. It also reports the title-bar regions for the platform's chrome hit test and the
//   rectangle left for the other preview modes.
// Why: this is the toolkit integration the owner asked to see: tree + layout + style sheet + router
//   + invalidator + text/icon rendering working together on the real window.
// Callers: PreviewApp (event loop, frame), Bench, tests/preview (headless layout checks). Calls:
//   ui-core (WidgetTree, FlexLayout, Router, Invalidator), ui-theme (Theme, StyleSheet), ui-text
//   via TextEngine/NameField, ui-render Painter via the paint functions.
// Units: the tree and all layout values are logical pixels; the viewport is given in physical
//   pixels with the display scale and every drawing call multiplies by that scale.
// Threading: UI thread only. Invariants: nodes_[widget.userData] is the payload of every widget;
//   the tree is only mutated through Scene methods; ids are re-validated by the toolkit, so a
//   stale id never reaches a node.
// Layout lifecycle: setViewport / setMode / text changes request layout through the Invalidator;
//   layout() runs it and must be called before paint() whenever needsFrame() is true.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Toolkit.h"
#include "NameField.h"
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
inline constexpr double kPanelWidth = 258.0;

struct SceneHost {
  std::function<bool(std::string_view)> writeClipboard;  // true when the text reached the clipboard
  std::function<std::optional<std::string>()> readClipboard;
};

// Where in the frame the time went, filled by Scene::layout (milliseconds, wall clock).
struct LayoutResult {
  bool ran = false;
  size_t nodesCommitted = 0;
};

class Scene final : public r1ui::core::layout::MeasureProvider {
 public:
  // Throws std::runtime_error when the tokens lack something the scene needs (message names it).
  Scene(std::shared_ptr<const r1ui::theme::Tokens> tokens, TextEngine& text, IconSet& icons, SceneHost host);
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
  void setWindowActive(bool active);
  float scale() const { return scale_; }

  // ---- input ----
  // Translates one platform event for the router; `nowMs` is a monotonic clock.
  void handleEvent(const r1ui::platform::Event& event, uint64_t nowMs);
  void setGlobalKeyHandler(r1ui::core::events::GlobalKeyHandler* handler);
  r1ui::core::events::Router& router() { return router_; }
  bool textFieldFocused() const;
  void clearFocus();
  r1ui::platform::CursorShape cursor() const { return cursor_; }

  // ---- frame ----
  bool needsFrame() const;
  // Discards every cached layout result; the next layout() recomputes the whole tree (benchmarks).
  void requestFullLayout() { invalidator_.requestFullLayout(); }
  LayoutResult layout();
  // Draws the whole tree. Call between beginFrame and endFrame of the target.
  void paint(r1ui::render::Painter& painter);
  // Advances time-driven state (caret blink); true when something needs repainting.
  bool tick(uint64_t nowMs);
  // Milliseconds until tick() has something to do; nullopt = nothing scheduled.
  std::optional<uint64_t> msUntilTick(uint64_t nowMs) const;

  // ---- results of the last layout ----
  r1ui::platform::ChromeLayout chromeLayout() const;
  // The area below the title bar, physical pixels (what the non-widget modes draw into).
  r1ui::render::Rect bodyRect() const;
  // Panel rectangle in logical pixels; empty when the panel is hidden.
  r1ui::core::layout::Rect panelRect() const;
  // Absolute logical rectangle of a widget, or empty for a stale id (tests, chrome, bench).
  r1ui::core::layout::Rect absRect(r1ui::core::tree::WidgetId id) const;
  const r1ui::core::tree::WidgetTree& tree() const { return tree_; }
  // Ids of notable widgets (tests and chrome registration).
  struct Ids {
    r1ui::core::tree::WidgetId root, titleBar, minimize, maximize, close, body, panel, nameField;
  };
  const Ids& ids() const { return ids_; }
  const std::string& nameValue() const;
  size_t widgetCount() const { return tree_.nodeCount(); }
  // Rasterises every icon the scene uses at the current scale so a missing or damaged icon file
  // is reported (std::runtime_error) before the first frame.
  void prepareIcons();

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
  void paintTextField(r1ui::render::Painter& painter, Node& node, const r1ui::render::Rect& rect);
  void selectSibling(WidgetId id);
  void scrollPanel(double deltaLogical);
  r1ui::render::Rect physical(const r1ui::core::layout::Rect& rect) const;
  r1ui::render::Color color(const r1ui::theme::Color& c, double opacity = 1.0) const;
  r1ui::render::Color themeColor(std::string_view name) const;

  std::shared_ptr<const r1ui::theme::Tokens> tokens_;
  TextEngine& text_;
  IconSet& icons_;
  SceneHost host_;
  r1ui::theme::Theme theme_;
  r1ui::theme::StyleSheet sheet_;
  r1ui::core::tree::WidgetTree tree_;
  r1ui::core::events::Router router_;
  r1ui::core::invalidation::Invalidator invalidator_;
  std::unique_ptr<r1ui::core::events::EventHandler> handler_;
  std::vector<Node> nodes_;
  std::unique_ptr<NameField> name_;
  Ids ids_;
  WidgetId panelContent_;
  WidgetId modeText_;
  std::array<WidgetId, kModeCount> squares_;

  Mode mode_ = Mode::Widgets;
  float scale_ = 1.0f;
  int viewportWidth_ = 0;
  int viewportHeight_ = 0;
  bool maximized_ = false;
  bool windowActive_ = true;
  double scrollY_ = 0.0;
  uint64_t nowMs_ = 0;
  r1ui::platform::CursorShape cursor_ = r1ui::platform::CursorShape::Arrow;
};

}  // namespace preview
