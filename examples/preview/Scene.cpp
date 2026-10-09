// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Scene construction, viewport/mode/theme state, input translation from platform events to
//   router calls, the widget event handler (hover, press, focus, selection, wheel), cursor choice,
//   layout driving and the chrome/body rectangles. Tree construction is in SceneBuilder (SceneBuild.cpp,
//   PanelBuild.cpp) and painting in ScenePaint.cpp.
// Invariants: widget state changes go through setStateBit so every visible change requests a
//   paint; ids are never cached across structural edits except the ones in Ids (the tree is built
//   once and never destroys those widgets).
// Callers: PreviewApp.cpp, Bench.cpp, tests/preview.
#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "SceneBuilder.h"
#include "r1ui/core/CheckedCast.h"

namespace preview {

namespace events = r1ui::core::events;
namespace layout = r1ui::core::layout;
namespace tree = r1ui::core::tree;
using r1ui::theme::State::kActive;
using r1ui::theme::State::kFocus;
using r1ui::theme::State::kHover;
using r1ui::theme::State::kSelected;

namespace {

constexpr double kWheelStepPx = 48.0;  // logical pixels scrolled per wheel notch

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

Scene::Scene(std::shared_ptr<const r1ui::theme::Tokens> tokens, TextEngine& text, IconSet& icons, SceneHost host)
    : tokens_(tokens),
      text_(text),
      icons_(icons),
      host_(std::move(host)),
      theme_(tokens),
      sheet_(makeSheet(*tokens)),
      router_(tree_, tree_.createRoot().id),
      invalidator_(tree_),
      handler_(std::make_unique<Handler>(*this)) {
  for (const char* name : {"canvas", "panel", "panel-secondary", "panel-field", "border", "accent", "surface", "muted"}) {
    if (!theme_.color(name)) throw std::runtime_error(std::string("The tokens lack the colour \"") + name + "\" the preview needs");
  }
  ids_.root = router_.root();
  r1ui::text::ClipboardCallbacks clipboard;
  clipboard.write = host_.writeClipboard;
  clipboard.read = host_.readClipboard;
  name_ = std::make_unique<NameField>(text_, std::move(clipboard), "Rectangle 1");
  SceneBuilder(*this).build();
  setMode(Mode::Widgets);
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

void Scene::prepareIcons() {
  for (const Node& n : nodes_) {
    if (n.icon.empty()) continue;
    icons_.prepare(n.icon, std::max(1, static_cast<int>(std::lround(static_cast<float>(n.iconPx) * scale_))));
  }
}

const std::string& Scene::nameValue() const { return name_->value(); }

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
  if (mode != Mode::Widgets) clearFocus();
  tree::Widget* panel = tree_.get(ids_.panel);
  if (panel != nullptr) {
    panel->style.display = mode == Mode::Widgets ? layout::Display::Flex : layout::Display::None;
    invalidator_.requestLayout(ids_.panel);
  }
  if (Node* n = nodeOf(modeText_)) {
    n->text = std::string("- ") + modeName(mode);
    invalidator_.requestLayout(modeText_);
  }
  for (int i = 0; i < kModeCount; ++i) setStateBit(squares_[static_cast<size_t>(i)], kSelected, static_cast<int>(mode) == i);
  invalidator_.requestPaint(ids_.root);
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
  if (maximized == maximized_) return;
  maximized_ = maximized;
  if (Node* n = nodeOf(ids_.maximize)) n->glyph = static_cast<int>(maximized ? ChromeGlyphKind::Restore : ChromeGlyphKind::Maximize);
  invalidator_.requestPaint(ids_.maximize);
}

void Scene::setWindowActive(bool active) {
  if (active == windowActive_) return;
  windowActive_ = active;
  invalidator_.requestPaint(ids_.nameField);
}

void Scene::setStateBit(WidgetId id, uint8_t bit, bool on) {
  Node* n = nodeOf(id);
  if (n == nullptr) return;
  const uint8_t next = on ? static_cast<uint8_t>(n->state | bit) : static_cast<uint8_t>(n->state & ~bit);
  if (next == n->state) return;
  n->state = next;
  invalidator_.requestPaint(id);
}

bool Scene::textFieldFocused() const { return router_.focused() == ids_.nameField && ids_.nameField.valid(); }

void Scene::clearFocus() { router_.clearFocus(); }

void Scene::setGlobalKeyHandler(events::GlobalKeyHandler* handler) { router_.setGlobalKeyHandler(handler); }

// ---- Input --------------------------------------------------------------------------------

void Scene::handleEvent(const r1ui::platform::Event& e, uint64_t nowMs) {
  using ET = r1ui::platform::EventType;
  nowMs_ = nowMs;
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
    case ET::FocusGained: setWindowActive(true); break;
    case ET::FocusLost:
      setWindowActive(false);
      router_.cancelPointerInteraction();
      break;
    default: break;
  }
}

void Scene::updateCursor() {
  r1ui::platform::CursorShape shape = r1ui::platform::CursorShape::Arrow;
  for (WidgetId id = router_.hovered(); id.valid(); id = tree_.parent(id)) {
    const Node* n = nodeOf(id);
    if (n != nullptr && n->cursor != r1ui::platform::CursorShape::Arrow) {
      shape = n->cursor;
      break;
    }
  }
  cursor_ = shape;
}

void Scene::selectSibling(WidgetId id) {
  const WidgetId parent = tree_.parent(id);
  tree_.forEachChild(parent, [&](WidgetId sibling) {
    const Node* n = nodeOf(sibling);
    if (n != nullptr && n->selectable) setStateBit(sibling, kSelected, sibling == id);
  });
}

void Scene::scrollPanel(double deltaLogical) {
  const layout::Rect panel = absRect(ids_.panel);
  const layout::Rect content = absRect(panelContent_);
  const double overflow = std::max(0.0, static_cast<double>(content.h) - static_cast<double>(panel.h));
  const double next = std::clamp(scrollY_ + deltaLogical, 0.0, overflow);
  if (next == scrollY_) return;
  scrollY_ = next;
  if (tree::Widget* w = tree_.get(panelContent_)) w->style.margin[layout::kTop] = layout::Length::px(-scrollY_);
  invalidator_.requestLayout(panelContent_);
}

void Scene::onWidgetEvent(events::Event& e) {
  Node* node = nodeOf(e.current);
  if (node == nullptr) return;
  using ET = events::EventType;
  const bool atTarget = e.current == e.target;
  switch (e.type) {
    case ET::PointerEnter:
      if (node->interactive) setStateBit(e.current, kHover, true);
      break;
    case ET::PointerLeave:
      if (node->interactive) setStateBit(e.current, static_cast<uint8_t>(kHover | kActive), false);
      break;
    case ET::PointerDown:
      if (e.button != events::Button::Left) break;
      if (node->interactive) setStateBit(e.current, kActive, true);
      if (node->kind == NodeKind::TextField && atTarget) {
        router_.focus(e.current, events::FocusReason::Pointer);
        name_->onPointerDown(static_cast<float>(e.localX * scale_), e.clickCount, (e.modifiers & events::Mod::kShift) != 0, nowMs_);
        router_.capturePointer(e.current);
        invalidator_.requestPaint(e.current);
      }
      break;
    case ET::PointerMove:
      if (node->kind == NodeKind::TextField && atTarget && (e.buttons & events::buttonBit(events::Button::Left)) != 0 &&
          router_.capturer() == e.current) {
        name_->onPointerDrag(static_cast<float>(e.localX * scale_), nowMs_);
        invalidator_.requestPaint(e.current);
      }
      break;
    case ET::PointerUp:
    case ET::CaptureLost:
      if (node->interactive) setStateBit(e.current, kActive, false);
      break;
    case ET::Click:
      if (node->selectable && atTarget) selectSibling(e.current);
      break;
    case ET::PointerWheel:
      if (e.current == ids_.panel) {
        scrollPanel(-e.wheelY * kWheelStepPx);
        e.markHandled();
      }
      break;
    case ET::FocusIn:
      if (node->kind == NodeKind::TextField) {
        setStateBit(e.current, kFocus, true);
        name_->setFocused(true, nowMs_);
      }
      break;
    case ET::FocusOut:
      if (node->kind == NodeKind::TextField) {
        setStateBit(e.current, kFocus, false);
        name_->setFocused(false, nowMs_);
        invalidator_.requestPaint(e.current);
      }
      break;
    case ET::KeyDown:
      if (node->kind != NodeKind::TextField) break;
      if (e.key == events::Key::Escape || e.key == events::Key::Enter) {
        router_.clearFocus();
        e.markHandled();
      } else if (name_->onKey(e.key, e.modifiers, nowMs_)) {
        invalidator_.requestPaint(e.current);
        e.markHandled();
      }
      break;
    case ET::TextInput:
      if (node->kind == NodeKind::TextField) {
        name_->onText(e.codePoint, nowMs_);
        invalidator_.requestPaint(e.current);
        e.markHandled();
      }
      break;
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

bool Scene::tick(uint64_t nowMs) {
  nowMs_ = nowMs;
  if (!windowActive_) return false;
  if (!name_->tick(nowMs)) return false;
  invalidator_.requestPaint(ids_.nameField);
  return true;
}

std::optional<uint64_t> Scene::msUntilTick(uint64_t nowMs) const {
  if (!windowActive_) return std::nullopt;
  return name_->msUntilTick(nowMs);
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

layout::Rect Scene::panelRect() const {
  const tree::Widget* w = tree_.get(ids_.panel);
  if (w == nullptr || w->style.display == layout::Display::None) return {};
  return w->absRect;
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
