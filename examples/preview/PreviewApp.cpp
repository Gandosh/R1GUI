// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PreviewApp.h.
// Keys (application level, reached through the router's global handler after the focused widget
//   declined them): Tab next mode, T dark/light, Escape quit (or cancel a dock tab drag), Left/Right
//   step reference screens, S/L/R dock layout save/load/reset. While the Name field has focus it
//   uses every printable key, so none of these fire; Escape/Enter leave the field first.
// Invariants: redraw_ is true whenever something outside the scene's own invalidation changed what
//   must be shown (mode, theme, screens, sandbox, resize); frame() clears it only after a frame was
//   presented. The window's chrome layout is refreshed after every layout pass.
// Callers: main.cpp, Bench.cpp.
#include "PreviewApp.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "r1ui/core/CheckedCast.h"

namespace preview {

namespace events = r1ui::core::events;
namespace platform = r1ui::platform;
using r1ui::render::Rect;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kMinLogicalWidth = 320;
constexpr int kMinLogicalHeight = 200;

// Letter keys carry their ASCII code (events::Key only names the first letter); the key switch
// below works on the numeric code.
constexpr uint32_t kKeyT = 'T';
constexpr uint32_t kKeyS = 'S';
constexpr uint32_t kKeyL = 'L';
constexpr uint32_t kKeyR = 'R';

double millis(Clock::time_point from, Clock::time_point to) { return std::chrono::duration<double, std::milli>(to - from).count(); }

r1ui::render::Color canvasColor(const Scene& scene) {
  const auto c = scene.theme().color("canvas");
  return c ? r1ui::render::Color::fromRgba8(c->r, c->g, c->b) : r1ui::render::Color{0, 0, 0, 1};
}

platform::MouseEvent legacyMouse(const platform::Event& e) {
  platform::MouseEvent m;
  m.button = e.button;
  m.x = e.x;
  m.y = e.y;
  switch (e.type) {
    case platform::EventType::MouseDown: m.type = platform::MouseEvent::Type::Down; break;
    case platform::EventType::MouseUp: m.type = platform::MouseEvent::Type::Up; break;
    case platform::EventType::CaptureLost: m.type = platform::MouseEvent::Type::CaptureLost; break;
    default: m.type = platform::MouseEvent::Type::Move; break;
  }
  return m;
}

}  // namespace

// ---- Construction ---------------------------------------------------------------------------

PreviewApp::PreviewApp(const AppOptions& options) : started_(Clock::now()), paths_{executableDir()} {
  tokens_ = std::make_shared<const r1ui::theme::Tokens>(loadTokens(paths_));

  platform::WindowDesc desc;
  desc.title = "R1GUI Preview";
  desc.width = options.width;
  desc.height = options.height;
  desc.borderless = true;
  desc.sizesAreLogical = options.logicalSize;
  desc.minSize = platform::Size{kMinLogicalWidth, kMinLogicalHeight};
  window_ = std::make_unique<platform::Window>(desc);

  device_ = std::make_unique<r1ui::render::RenderDevice>();
  target_ = std::make_unique<r1ui::render::WindowTarget>(*device_, *window_, r1ui::render::WindowTargetOptions{options.presentMode});
  textures_ = std::make_unique<GpuTextureFactory>(*device_);
  text_ = std::make_unique<TextEngine>(*textures_, paths_.fonts());
  icons_ = std::make_unique<IconSet>(*textures_, std::vector<std::filesystem::path>{paths_.icons(), paths_.customIcons()});

  SceneHost host;
  host.writeClipboard = [this](std::string_view text) {
    return window_->setClipboardText(text) == platform::ClipboardStatus::Ok;
  };
  host.readClipboard = [this]() -> std::optional<std::string> {
    platform::ClipboardText clip = window_->getClipboardText();
    if (clip.status != platform::ClipboardStatus::Ok) return std::nullopt;
    return std::move(clip.text);
  };
  scene_ = std::make_unique<Scene>(tokens_, *text_, *icons_, std::move(host));
  scene_->setGlobalKeyHandler(this);
  swatches_ = std::make_unique<SwatchesView>(*tokens_);
  screens_ = std::make_unique<ScreensView>(*device_, paths_.references());
  sandbox_ = std::make_unique<DockSandbox>(*window_, *tokens_, paths_.layoutFile());

  syncViewport();
  scene_->layout();
  window_->setChromeLayout(scene_->chromeLayout());
  scene_->prepareIcons();  // a missing icon file surfaces now, before the first frame
  setMode(options.mode);

  // While Windows runs its own move/size loop pumpEvents() does not return; the window calls back so
  // the content keeps following the window.
  window_->setLiveCallback([this] { liveFrame(); });
}

PreviewApp::~PreviewApp() {
  if (window_) window_->setLiveCallback({});  // nothing may call back into a half-destroyed app
  if (device_ && !device_->lost()) device_->waitIdle();
}

uint64_t PreviewApp::nowMs() const { return r1ui::core::checkedCast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started_).count()); }

// ---- Modes and keys -------------------------------------------------------------------------

void PreviewApp::setMode(Mode mode) {
  scene_->setMode(mode);
  swatches_->clearHover();
  redraw_ = true;
  updateWindowTitle();
}

void PreviewApp::updateWindowTitle() {
  std::string title = std::string("R1GUI Preview - ") + modeName(mode());
  if (mode() == Mode::Sandbox) title = sandbox_->title();
  if (title == windowTitle_) return;
  window_->setTitle(title);
  windowTitle_ = std::move(title);
}

bool PreviewApp::onGlobalKey(const events::Event& e, events::Router&) {
  namespace keys = platform::keys;
  if ((e.modifiers & (events::Mod::kCtrl | events::Mod::kAlt | events::Mod::kMeta)) != 0) return false;
  const auto vk = static_cast<uint32_t>(e.key);
  switch (vk) {
    case static_cast<uint32_t>(events::Key::Escape):
      if (mode() == Mode::Sandbox && sandbox_->onKey(keys::kEscape)) {
        redraw_ = true;
        return true;
      }
      quit_ = true;
      return true;
    case kKeyT:
      if (e.repeat) return true;
      scene_->toggleTheme();
      redraw_ = true;
      return true;
    case static_cast<uint32_t>(events::Key::Left):
    case static_cast<uint32_t>(events::Key::Right):
      if (mode() != Mode::Screens) return false;
      screens_->step(e.key == events::Key::Left ? -1 : +1);
      redraw_ = true;
      return true;
    case kKeyS:
    case kKeyL:
    case kKeyR:
      if (mode() != Mode::Sandbox) return false;
      redraw_ = sandbox_->onKey(vk) || redraw_;
      updateWindowTitle();
      return true;
    default: return false;
  }
}

// ---- Events ---------------------------------------------------------------------------------

void PreviewApp::syncViewport() {
  scene_->setViewport(window_->clientWidth(), window_->clientHeight(), window_->dpiScale());
}

void PreviewApp::routeToMode(const platform::Event& e) {
  const Rect body = scene_->bodyRect();
  using ET = platform::EventType;
  switch (mode()) {
    case Mode::Sandbox:
      if (e.type == ET::MouseMove || e.type == ET::MouseDown || e.type == ET::MouseUp || e.type == ET::CaptureLost) {
        sandbox_->setBounds(body);
        sandbox_->onMouse(legacyMouse(e));
        redraw_ = true;
        updateWindowTitle();
      }
      break;
    case Mode::Swatches:
      if (e.type == ET::MouseMove) redraw_ = swatches_->onPointer(e.x, e.y, body, scene_->scale()) || redraw_;
      if (e.type == ET::MouseLeave) {
        swatches_->clearHover();
        redraw_ = true;
      }
      break;
    case Mode::Screens:
      if (e.type == ET::MouseDown && e.button == platform::MouseButton::Left && e.y >= body.y) {
        screens_->onClick(e.x, body);
        redraw_ = true;
      }
      break;
    case Mode::Widgets: break;
  }
}

void PreviewApp::processEvents() {
  const uint64_t now = nowMs();
  using ET = platform::EventType;
  for (const platform::Event& e : window_->takeEvents()) {
    switch (e.type) {
      case ET::Resized:
      case ET::DpiChanged:
        syncViewport();
        scene_->setMaximized(window_->isMaximized());
        redraw_ = true;
        break;
      case ET::KeyDown:
        if (e.virtualKey == VK_TAB && !e.modifiers.ctrl && !e.modifiers.alt && !e.modifiers.meta) {
          if (!e.repeat) setMode(static_cast<Mode>((static_cast<int>(mode()) + 1) % kModeCount));
          break;
        }
        scene_->handleEvent(e, now);
        break;
      case ET::MouseMove:
      case ET::MouseDown:
      case ET::MouseUp:
      case ET::MouseLeave:
      case ET::CaptureLost:
        if (e.type != ET::MouseLeave && e.type != ET::CaptureLost) {
          pointerX_ = e.x;
          pointerY_ = e.y;
          const Rect body = scene_->bodyRect();
          pointerInBody_ = e.x >= body.x && e.y >= body.y && e.x < body.x + body.w && e.y < body.y + body.h;
        }
        scene_->handleEvent(e, now);
        routeToMode(e);
        break;
      case ET::CloseRequested: break;  // the window closes itself (default policy)
      default: scene_->handleEvent(e, now); break;
    }
  }
  scene_->tick(now);  // caret blink: the scene schedules its own repaint
  applyCursor();
}

void PreviewApp::applyCursor() {
  // The docking sandbox picks resize cursors over its splitters; everywhere else the scene decides.
  if (mode() == Mode::Sandbox && pointerInBody_) return;
  window_->setCursor(scene_->cursor());
}

// ---- Frame ----------------------------------------------------------------------------------

void PreviewApp::paintMode(r1ui::render::Painter& painter) {
  const Rect body = scene_->bodyRect();
  if (body.w < 1.0f || body.h < 1.0f) return;
  painter.pushClip(body);
  const auto theme = scene_->theme().id();
  switch (mode()) {
    case Mode::Swatches: swatches_->paint(painter, *text_, theme, body, scene_->scale()); break;
    case Mode::Screens: screens_->paint(painter, *text_, theme, body, scene_->scale()); break;
    case Mode::Sandbox:
      sandbox_->setBounds(body);
      sandbox_->draw(painter, *text_, theme);
      break;
    case Mode::Widgets: break;
  }
  painter.popClip();
}

bool PreviewApp::frame(FrameTimes* times) {
  if (inFrame_) return false;  // never re-entered (the live callback can fire while a frame runs)
  struct Guard {
    bool& flag;
    explicit Guard(bool& f) : flag(f) { flag = true; }
    ~Guard() { flag = false; }
  } guard(inFrame_);

  const auto t0 = Clock::now();
  syncViewport();
  const LayoutResult layoutResult = scene_->layout();
  if (layoutResult.ran) window_->setChromeLayout(scene_->chromeLayout());
  const auto t1 = Clock::now();
  if (!target_->beginFrame(canvasColor(*scene_))) {
    redraw_ = true;  // layout already consumed this frame's damage: draw again once the target is ready
    return false;
  }
  scene_->paint(target_->painter());
  paintMode(target_->painter());
  text_->uploadAtlas();
  const auto t2 = Clock::now();
  const bool presented = target_->endFrame();
  const auto t3 = Clock::now();
  const bool atlasOverflowed = text_->consumeAtlasOverflow();  // the atlas was reset: draw once more
  if (presented) {
    redraw_ = atlasOverflowed;
    if (framesPresented_++ == 0) startupMs_ = millis(started_, t3);
  } else {
    redraw_ = true;  // not presented (swapchain out of date): the damage is gone, so draw again
  }
  if (times != nullptr) {
    const r1ui::render::FrameTimings gpu = target_->lastFrameTimings();
    times->layoutMs = millis(t0, t1);
    times->paintMs = millis(t1, t2);
    times->waitMs = gpu.waitMs;
    times->recordSubmitMs = gpu.recordSubmitMs;
    times->presentMs = gpu.presentMs;
    times->totalMs = millis(t0, t3);
    times->presented = presented;
  }
  return presented;
}

void PreviewApp::liveFrame() {
  if (pending_ != nullptr) return;
  try {
    processEvents();
    if (!window_->clientWidth() || !window_->clientHeight()) return;
    if (redraw_ || scene_->needsFrame()) frame();
  } catch (...) {
    pending_ = std::current_exception();
  }
}

void PreviewApp::rethrowPending() {
  if (pending_ == nullptr) return;
  const std::exception_ptr error = pending_;
  pending_ = nullptr;
  std::rethrow_exception(error);
}

bool PreviewApp::pumpMessages() {
  const bool open = window_->pumpEvents();
  rethrowPending();
  if (!open) return false;
  processEvents();
  return !quit_;
}

bool PreviewApp::step(bool block, unsigned maxWaitMs) {
  if (!pumpMessages()) return false;
  const bool canDraw = window_->clientWidth() > 0 && window_->clientHeight() > 0;
  if (canDraw && (redraw_ || scene_->needsFrame())) {
    frame();
    return true;
  }
  if (block) {
    const std::optional<uint64_t> wake = scene_->msUntilTick(nowMs());
    const unsigned scheduled = wake ? static_cast<unsigned>(std::min<uint64_t>(*wake, 0x7FFFFFFFu)) : platform::kWaitForever;
    window_->waitForEvents(std::min(scheduled, maxWaitMs));
  }
  return true;
}

void PreviewApp::run() {
  while (step(true)) {
  }
}

}  // namespace preview
