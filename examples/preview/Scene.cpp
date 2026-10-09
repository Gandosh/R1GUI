// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Scene construction, viewport/mode/theme state, input translation from platform events to
//   router calls, the widget event handler (hover and press of the caption buttons), cursor choice,
//   layout driving and the chrome/body rectangles. Tree construction is in SceneBuilder
//   (SceneBuild.cpp) and painting in ScenePaint.cpp.
// Invariants: widget state changes go through setStateBit so every visible change requests a
//   paint; the tree is built once and never changes structure.
// Callers: PreviewApp.cpp, Bench.cpp, tests/preview.
#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "SceneBuilder.h"

namespace preview {

namespace events = r1ui::core::events;
namespace layout = r1ui::core::layout;
namespace tree = r1ui::core::tree;
using r1ui::theme::State::kActive;
using r1ui::theme::State::kHover;
using r1ui::theme::State::kSelected;

namespace {

r1ui::theme::StyleSheet makeSheet(const r1ui::theme::Tokens& tokens) {
  const std::vector<r1ui::theme::StyleRuleEntry> rules = allRules();
  auto result = r1ui::theme::StyleSheet::create(rules, tokens);
  if (!result.ok()) {
    std::string message = "The style table was rejected:";
    for (const std::string& e : result.errors) message += "\n  " + e;
    throw std::runtime_error(message);
  }
  return std::move(*result.sheet);
}

uint8_t routerModifiers(const r1ui::platform::Modifiers& m) {
  return static_cast<uint8_t>((m.shift ? events::Mod::kShift : 0) | (m.ctrl ? events::Mod::kCtrl : 0) |
                              (m.alt ? events::Mod::kAlt : 0) | (m.meta ? events::Mod::kMeta : 0));
}

events::Button routerButton(r1ui::platform::MouseButton b) {
  using r1ui::platform::MouseButton;
  switch (b) {
    case MouseButton::Left: return events::Button::Left;
    case MouseButton::Middle: return events::Button::Middle;
    case MouseButton::Right: return events::Button::Right;
    case MouseButton::X1: return events::Button::X1;
    case MouseButton::X2: return events::Button::X2;
  }
  return events::Button::None;
}

// Windows virtual-key codes the router knows by name; every other key is Unknown.
events::Key routerKey(uint32_t vk) {
  const bool named = vk == 8 || vk == 9 || vk == 13 || vk == 27 || vk == 32 || (vk >= 33 && vk <= 40) || vk == 45 ||
                     vk == 46 || (vk >= 48 && vk <= 57) || (vk >= 65 && vk <= 90) || (vk >= 112 && vk <= 123);
  return named ? static_cast<events::Key>(vk) : events::Key::Unknown;
}

}  // namespace

// ---- Handler ------------------------------------------------------------------------------

class Scene::Handler final : public events::EventHandler {
 public:
  explicit Handler(Scene& scene) : scene_(scene) {}
  void onEvent(events::Event& event, events::Router&) override { scene_.onWidgetEvent(event); }

 private:
  Scene& scene_;
};

// ---- Construction -------------------------------------------------------------------------

Scene::Scene(r1ui::theme::Theme& theme, TextEngine& text)
    : theme_(theme),
      text_(text),
      sheet_(makeSheet(theme.tokens())),
      router_(tree_, tree_.createRoot().id),
      invalidator_(tree_),
      handler_(std::make_unique<Handler>(*this)) {
  for (const char* name : {"canvas", "panel-secondary", "border", "accent", "surface", "muted", "hover"}) {
    if (!theme_.color(name)) throw std::runtime_error(std::string("The tokens lack the colour \"") + name + "\" the preview needs");
  }
  ids_.root = router_.root();
  SceneBuilder(*this).build();
  setMode(Mode::Editor);
  invalidator_.setRoot(ids_.root, 0.0, 0.0);
}

Scene::~Scene() {
  // Handlers are non-owning pointers inside the widgets; the tree dies with the scene, but clear
  // them first so nothing can dispatch into a half-destroyed handler.
  tree_.forEachDescendant(
      ids_.root, [&](tree::WidgetId id) { tree_.get(id)->handler = nullptr; }, true);
}

Node* Scene::nodeOf(WidgetId id) {
  tree::Widget* w = tree_.get(id);
  return w != nullptr && w->userData < nodes_.size() ? &nodes_[static_cast<size_t>(w->userData)] : nullptr;
}

const Node* Scene::nodeOf(WidgetId id) const {
  const tree::Widget* w = tree_.get(id);
  return w != nullptr && w->userData < nodes_.size() ? &nodes_[static_cast<size_t>(w->userData)] : nullptr;
}

const r1ui::theme::ResolvedStyle& Scene::resolved(CachedStyle& cache, const std::string& key, uint8_t state) {
  if (!cache.value || cache.revision != theme_.revision() || cache.state != state) {
    cache.value = sheet_.resolve(theme_, key, state);
    if (!cache.value) throw std::logic_error("unknown style key \"" + key + "\"");
    cache.revision = theme_.revision();
    cache.state = state;
  }
  return *cache.value;
}

r1ui::render::Color Scene::color(const r1ui::theme::Color& c, double opacity) const {
  return r1ui::render::Color::fromRgba8(c.r, c.g, c.b, static_cast<uint8_t>(std::lround(c.a * std::clamp(opacity, 0.0, 1.0))));
}

r1ui::render::Color Scene::themeColor(std::string_view name) const {
  const auto c = theme_.color(name);
  return c ? color(*c) : r1ui::render::Color{1.0f, 0.0f, 1.0f, 1.0f};  // magenta: a token that vanished
}

r1ui::render::Rect Scene::physical(const layout::Rect& r) const {
  return {static_cast<float>(r.x) * scale_, static_cast<float>(r.y) * scale_, static_cast<float>(r.w) * scale_,
          static_cast<float>(r.h) * scale_};
}

// ---- State --------------------------------------------------------------------------------

void Scene::toggleTheme() {
  theme_.toggle();
  invalidator_.requestPaint(ids_.root);
}

void Scene::setMode(Mode mode) {
  mode_ = mode;
  if (Node* n = nodeOf(modeText_)) {
    n->text = std::string("- ") + modeName(mode);
    invalidator_.requestLayout(modeText_);
  }
  for (int i = 0; i < kModeCount; ++i) setStateBit(squares_[static_cast<size_t>(i)], kSelected, static_cast<int>(mode) == i);
  invalidator_.requestPaint(ids_.root);
}

uint8_t Scene::squareState(int index) const {
  if (index < 0 || index >= kModeCount) return 0;
  const Node* n = nodeOf(squares_[static_cast<size_t>(index)]);
  return n != nullptr ? n->state : 0;
}

void Scene::setViewport(int width, int height, float scale) {
  const float safeScale = std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
  if (width == viewportWidth_ && height == viewportHeight_ && safeScale == scale_) return;
  const bool scaleChanged = safeScale != scale_;
  viewportWidth_ = std::max(0, width);
  viewportHeight_ = std::max(0, height);
  scale_ = safeScale;
  invalidator_.setRoot(ids_.root, viewportWidth_ / scale_, viewportHeight_ / scale_);
  if (scaleChanged) invalidator_.requestFullLayout();
  invalidator_.requestPaint(ids_.root);
}

void Scene::setMaximized(bool maximized) {
  Node* n = nodeOf(ids_.maximize);
  const int glyph = static_cast<int>(maximized ? ChromeGlyphKind::Restore : ChromeGlyphKind::Maximize);
  if (n == nullptr || n->glyph == glyph) return;
  n->glyph = glyph;
  invalidator_.requestPaint(ids_.maximize);
}

void Scene::setStateBit(WidgetId id, uint8_t bit, bool on) {
  Node* n = nodeOf(id);
  if (n == nullptr) return;
  const uint8_t next = on ? static_cast<uint8_t>(n->state | bit) : static_cast<uint8_t>(n->state & ~bit);
  if (next == n->state) return;
  n->state = next;
  invalidator_.requestPaint(id);
}

void Scene::setGlobalKeyHandler(events::GlobalKeyHandler* handler) { router_.setGlobalKeyHandler(handler); }

// ---- Input --------------------------------------------------------------------------------

void Scene::handleEvent(const r1ui::platform::Event& e, uint64_t nowMs) {
  using ET = r1ui::platform::EventType;
  const double inverse = 1.0 / static_cast<double>(scale_);
  const auto pointer = [&](events::Button button) {
    events::PointerInput in;
    in.x = static_cast<double>(e.x) * inverse;
    in.y = static_cast<double>(e.y) * inverse;
    in.button = button;
    in.modifiers = routerModifiers(e.modifiers);
    in.timestampMs = nowMs;
    return in;
  };
  const uint8_t mods = routerModifiers(e.modifiers);
  switch (e.type) {
    case ET::MouseMove:
      router_.pointerMove(pointer(events::Button::None));
      updateCursor();
      break;
    case ET::MouseDown: router_.pointerDown(pointer(routerButton(e.button))); break;
    case ET::MouseUp:
      router_.pointerUp(pointer(routerButton(e.button)));
      updateCursor();
      break;
    case ET::Wheel: {
      events::PointerInput in = pointer(events::Button::None);
      in.wheelX = static_cast<double>(e.wheelX);
      in.wheelY = static_cast<double>(e.wheelY);
      router_.pointerWheel(in);
      break;
    }
    case ET::MouseLeave:
      router_.pointerLeftWindow();
      cursor_ = r1ui::platform::CursorShape::Arrow;
      break;
    case ET::CaptureLost: router_.cancelPointerInteraction(); break;
    case ET::KeyDown: router_.keyDown(routerKey(e.virtualKey), mods, e.repeat, nowMs); break;
    case ET::KeyUp: router_.keyUp(routerKey(e.virtualKey), mods, nowMs); break;
    case ET::Char: router_.textInput(e.codePoint, mods, nowMs); break;
    case ET::FocusLost: router_.cancelPointerInteraction(); break;
    default: break;
  }
}

void Scene::updateCursor() { cursor_ = r1ui::platform::CursorShape::Arrow; }

void Scene::onWidgetEvent(events::Event& e) {
  Node* node = nodeOf(e.current);
  if (node == nullptr || !node->interactive) return;
  using ET = events::EventType;
  switch (e.type) {
    case ET::PointerEnter: setStateBit(e.current, kHover, true); break;
    case ET::PointerLeave: setStateBit(e.current, static_cast<uint8_t>(kHover | kActive), false); break;
    case ET::PointerDown:
      if (e.button == events::Button::Left) setStateBit(e.current, kActive, true);
      break;
    case ET::PointerUp:
    case ET::CaptureLost: setStateBit(e.current, kActive, false); break;
    default: break;
  }
}

// ---- Frame --------------------------------------------------------------------------------

bool Scene::needsFrame() const { return invalidator_.needsFrame(); }

LayoutResult Scene::layout() {
  const auto frame = invalidator_.runFrame(this);
  if (frame.layoutRan) router_.sync();
  return {frame.layoutRan, frame.layoutStats.nodesCommitted};
}

layout::MeasureResult Scene::measure(WidgetId widget, const layout::MeasureInput&) {
  Node* n = nodeOf(widget);
  if (n == nullptr || n->kind != NodeKind::Text) return {};
  const r1ui::theme::ResolvedStyle& style =
      n->textStyle.empty() ? resolved(n->boxCache, n->style, n->state) : resolved(n->textCache, n->textStyle, r1ui::theme::State::kNone);
  const float px = static_cast<float>(style.text.fontSize * static_cast<double>(scale_));
  const double width = static_cast<double>(text_.measure(n->text, px, style.text.weight)) / static_cast<double>(scale_);
  return {width, style.text.lineHeight};
}

// ---- Results ------------------------------------------------------------------------------

layout::Rect Scene::absRect(WidgetId id) const {
  const tree::Widget* w = tree_.get(id);
  return w != nullptr ? w->absRect : layout::Rect{};
}

r1ui::render::Rect Scene::bodyRect() const { return physical(absRect(ids_.body)); }

r1ui::platform::ChromeLayout Scene::chromeLayout() const {
  const auto toPhysical = [&](WidgetId id) {
    const layout::Rect r = absRect(id);
    const auto scaled = [&](int32_t v) { return static_cast<int>(std::lround(static_cast<double>(v) * static_cast<double>(scale_))); };
    const int x0 = scaled(r.x);
    const int y0 = scaled(r.y);
    return r1ui::platform::Rect{x0, y0, scaled(r.x + r.w) - x0, scaled(r.y + r.h) - y0};
  };
  r1ui::platform::ChromeLayout chrome;
  chrome.captionRects.push_back(toPhysical(ids_.titleBar));
  chrome.minimizeButton = toPhysical(ids_.minimize);
  chrome.maximizeButton = toPhysical(ids_.maximize);
  chrome.closeButton = toPhysical(ids_.close);
  return chrome;
}

}  // namespace preview
