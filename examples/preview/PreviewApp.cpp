// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PreviewApp.h: construction, mode switching, key policy, event routing to
//   the title-bar scene, the active UiContext and the direct-drawn modes, the frame and the loop.
// Invariants: redraw_ is true whenever something outside the scene's and the active context's own
//   invalidation changed what must be shown (mode, theme, screens, sandbox, resize); frame() clears
//   it only after a frame was presented. The window's chrome layout is refreshed after every shell
//   layout pass. Only the active mode's context receives input and frames; the other context keeps
//   its widgets and catches up (layout, theme) when it is shown again.
// Callers: main.cpp, Bench.cpp, tests/preview.
#include "PreviewApp.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "ComposedUtil.h"
#include "r1ui/core/CheckedCast.h"

namespace preview {

namespace events = r1ui::core::events;
namespace platform = r1ui::platform;
namespace widgets = r1ui::widgets;
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

platform::CursorShape platformCursor(widgets::Cursor cursor) {
  switch (cursor) {
    case widgets::Cursor::Pointer: return platform::CursorShape::Hand;
    case widgets::Cursor::Text: return platform::CursorShape::Text;
    case widgets::Cursor::ResizeHorizontal: return platform::CursorShape::ResizeHorizontal;
    case widgets::Cursor::ResizeVertical: return platform::CursorShape::ResizeVertical;
    case widgets::Cursor::ResizeNwSe: return platform::CursorShape::ResizeNwSe;
    case widgets::Cursor::ResizeNeSw: return platform::CursorShape::ResizeNeSw;
    case widgets::Cursor::Move: return platform::CursorShape::Move;
    case widgets::Cursor::NotAllowed: return platform::CursorShape::NotAllowed;
    case widgets::Cursor::Wait: return platform::CursorShape::Wait;
    case widgets::Cursor::Default: break;
  }
  return platform::CursorShape::Arrow;
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
  services_ = std::make_unique<widgets::Services>(tokens_, *textures_, widgets::ServicesPaths{paths_.fonts(), {paths_.icons(), paths_.customIcons()}});
  windowAtlas_ = std::make_unique<widgets::AtlasConsumer>(services_->text());
  scene_ = std::make_unique<Scene>(services_->theme(), services_->text());
  scene_->setGlobalKeyHandler(this);
  swatches_ = std::make_unique<SwatchesView>(*tokens_);
  screens_ = std::make_unique<ScreensView>(*device_, paths_.references());
  sandbox_ = std::make_unique<DockSandbox>(*window_, *tokens_, paths_.layoutFile());

  syncViewport();
  scene_->layout();
  window_->setChromeLayout(scene_->chromeLayout());
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

// A context for one widget mode: column root with the title bar's height reserved at the top, and a
// row container that fills the rest (the mode's screen goes in there).
std::unique_ptr<widgets::UiContext> PreviewApp::makeContext(r1ui::core::tree::WidgetId& content) {
  widgets::UiContextOptions options;
  options.host.writeClipboard = [this](std::string_view text) { window_->setClipboardText(text); };
  options.host.readClipboard = [this]() -> std::optional<std::string> {
    platform::ClipboardText clip = window_->getClipboardText();
    if (clip.status != platform::ClipboardStatus::Ok) return std::nullopt;
    return std::move(clip.text);
  };
  auto ui = std::make_unique<widgets::UiContext>(*services_, options);
  ui->setGlobalKeyHandler(this);
  ui->setAtlasConsumer(windowAtlas_.get());
  ui->setFrameLoopRunning(true);
  ui->rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
  ui->rootStyle().padding[r1ui::core::layout::kTop] = kTitleBarHeight;
  widgets::SectionBox& area = build::flex(*ui, ui->root(), true);
  build::grow(area.style());
  content = area.id();
  ui->setViewport(window_->clientWidth(), window_->clientHeight(), window_->dpiScale());
  return ui;
}

void PreviewApp::ensureModeBuilt(Mode mode) {
  r1ui::core::tree::WidgetId content;
  if (mode == Mode::Gallery && !galleryUi_) {
    galleryUi_ = makeContext(content);
    gallery_ = std::make_unique<GalleryApp>(*galleryUi_, content);
  } else if (mode == Mode::Widgets && !widgetsUi_) {
    widgetsUi_ = makeContext(content);
    ComposedHost host;
    host.setDarkTheme = [this](bool dark) { setDarkTheme(dark); };
    host.isDark = [this] { return scene_->theme().id() == r1ui::theme::ThemeId::Dark; };
    host.quit = [this] { quit_ = true; };
    composed_ = std::make_unique<ComposedApp>(*widgetsUi_, content, std::move(host));
  }
}

widgets::UiContext* PreviewApp::activeUi() {
  switch (mode()) {
    case Mode::Gallery: return galleryUi_.get();
    case Mode::Widgets: return widgetsUi_.get();
    default: return nullptr;
  }
}

// ---- Modes and keys -------------------------------------------------------------------------

void PreviewApp::setMode(Mode mode) {
  if (widgets::UiContext* leaving = activeUi()) {
    leaving->overlays().closeAll();  // no popup, menu or tooltip survives a switch away
    leaving->clearFocus();
  }
  ensureModeBuilt(mode);
  scene_->setMode(mode);
  swatches_->clearHover();
  if (widgets::UiContext* ui = activeUi()) {
    ui->setTime(nowMs());
    ui->setWindowActive(true);
  }
  if (composed_) composed_->syncTheme();
  redraw_ = true;
  updateWindowTitle();
}

void PreviewApp::setDarkTheme(bool dark) {
  if ((scene_->theme().id() == r1ui::theme::ThemeId::Dark) == dark) return;
  scene_->toggleTheme();
  if (composed_) composed_->syncTheme();
  redraw_ = true;
}

void PreviewApp::updateWindowTitle() {
  std::string title = std::string("R1GUI Preview - ") + modeName(mode());
  if (mode() == Mode::Sandbox) title = sandbox_->title();
  if (title == windowTitle_) return;
  window_->setTitle(title);
  windowTitle_ = std::move(title);
}

bool PreviewApp::onGlobalKey(const events::Event& e, events::Router& router) {
  namespace keys = platform::keys;
  if ((e.modifiers & (events::Mod::kCtrl | events::Mod::kAlt | events::Mod::kMeta)) != 0) return false;
  widgets::UiContext* ui = activeUi();
  const bool fromWidgets = ui != nullptr && &router == &ui->router();
  const auto vk = static_cast<uint32_t>(e.key);
  switch (vk) {
    case static_cast<uint32_t>(events::Key::Escape):
      if (fromWidgets && router.focused().valid()) {
        ui->clearFocus();  // the first Escape only leaves the focused widget
        return true;
      }
      if (mode() == Mode::Sandbox && sandbox_->onKey(keys::kEscape)) {
        redraw_ = true;
        return true;
      }
      quit_ = true;
      return true;
    case kKeyT:
      if (e.repeat) return true;
      setDarkTheme(scene_->theme().id() != r1ui::theme::ThemeId::Dark);
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

// Plain Tab cycles the modes unless a widget has focus or a popup is open (then it is focus
// navigation, which dialogs and fields need); Ctrl+Tab always cycles.
bool PreviewApp::tabCyclesModes(const platform::Event& e) {
  if (e.virtualKey != VK_TAB || e.modifiers.alt || e.modifiers.meta) return false;
  if (e.modifiers.ctrl) return true;
  widgets::UiContext* ui = activeUi();
  if (ui == nullptr) return true;
  return !ui->router().focused().valid() && ui->overlays().stack().empty();
}

// ---- Events ---------------------------------------------------------------------------------

void PreviewApp::syncViewport() {
  scene_->setViewport(window_->clientWidth(), window_->clientHeight(), window_->dpiScale());
  for (widgets::UiContext* ui : {galleryUi_.get(), widgetsUi_.get()}) {
    if (ui != nullptr) ui->setViewport(window_->clientWidth(), window_->clientHeight(), window_->dpiScale());
  }
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
    case Mode::Gallery:
    case Mode::Widgets: break;
  }
}

void PreviewApp::processEvents() {
  const uint64_t now = nowMs();
  using ET = platform::EventType;
  widgets::UiContext* ui = activeUi();
  if (ui != nullptr) ui->setTime(now);
  for (const platform::Event& e : window_->takeEvents()) {
    switch (e.type) {
      case ET::Resized:
      case ET::DpiChanged:
        syncViewport();
        scene_->setMaximized(window_->isMaximized());
        redraw_ = true;
        break;
      case ET::KeyDown:
        if (tabCyclesModes(e)) {
          if (!e.repeat) setMode(static_cast<Mode>((static_cast<int>(mode()) + (e.modifiers.shift ? kModeCount - 1 : 1)) % kModeCount));
          ui = activeUi();
          if (ui != nullptr) ui->setTime(now);
          break;
        }
        if (ui != nullptr) ui->handlePlatformEvent(e);
        else scene_->handleEvent(e, now);
        break;
      case ET::KeyUp:
      case ET::Char:
        if (ui != nullptr) ui->handlePlatformEvent(e);
        else scene_->handleEvent(e, now);
        break;
      case ET::MouseMove:
      case ET::MouseDown:
      case ET::MouseUp:
      case ET::MouseLeave:
      case ET::CaptureLost:
      case ET::Wheel:
        if (e.type != ET::MouseLeave && e.type != ET::CaptureLost && e.type != ET::Wheel) {
          pointerX_ = e.x;
          pointerY_ = e.y;
          const Rect body = scene_->bodyRect();
          pointerInBody_ = e.x >= body.x && e.y >= body.y && e.x < body.x + body.w && e.y < body.y + body.h;
        }
        scene_->handleEvent(e, now);
        if (ui != nullptr) ui->handlePlatformEvent(e);
        routeToMode(e);
        break;
      case ET::FocusGained:
      case ET::FocusLost:
        scene_->handleEvent(e, now);
        for (widgets::UiContext* each : {galleryUi_.get(), widgetsUi_.get()}) {
          if (each != nullptr) each->handlePlatformEvent(e);
        }
        redraw_ = true;
        break;
      case ET::CloseRequested: break;  // the window closes itself (default policy)
      default: scene_->handleEvent(e, now); break;
    }
  }
  if (ui != nullptr && ui->tick()) redraw_ = true;  // a due timer (tooltip, toast, menu delay) changed something
  applyCursor();
}

void PreviewApp::applyCursor() {
  // The docking sandbox picks resize cursors over its splitters; everywhere else the scene or the
  // widget under the pointer decides.
  if (mode() == Mode::Sandbox && pointerInBody_) return;
  if (const widgets::UiContext* ui = activeUi(); ui != nullptr && pointerInBody_) {
    window_->setCursor(platformCursor(ui->cursor()));
    return;
  }
  window_->setCursor(scene_->cursor());
}

// ---- Frame ----------------------------------------------------------------------------------

void PreviewApp::paintMode(r1ui::render::Painter& painter) {
  const Rect body = scene_->bodyRect();
  if (body.w < 1.0f || body.h < 1.0f) return;
  painter.pushClip(body);
  const auto theme = scene_->theme().id();
  switch (mode()) {
    case Mode::Swatches: swatches_->paint(painter, services_->text(), theme, body, scene_->scale()); break;
    case Mode::Screens: screens_->paint(painter, services_->text(), theme, body, scene_->scale()); break;
    case Mode::Sandbox:
      sandbox_->setBounds(body);
      sandbox_->draw(painter, services_->text(), theme);
      break;
    case Mode::Gallery:
    case Mode::Widgets: break;
  }
  painter.popClip();
}

bool PreviewApp::needsFrame() {
  const widgets::UiContext* ui = activeUi();
  return redraw_ || scene_->needsFrame() || (ui != nullptr && ui->needsFrame());
}

void PreviewApp::requestFullLayout() {
  scene_->requestFullLayout();
  if (widgets::UiContext* ui = activeUi()) ui->requestFullLayout();
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
  widgets::UiContext* ui = activeUi();
  const LayoutResult layoutResult = scene_->layout();
  if (ui != nullptr) {
    ui->setTime(nowMs());
    ui->frame();
  }
  if (layoutResult.ran) window_->setChromeLayout(scene_->chromeLayout());
  const auto t1 = Clock::now();
  if (!target_->beginFrame(canvasColor(*scene_))) {
    redraw_ = true;  // layout already consumed this frame's damage: draw again once the target is ready
    return false;
  }
  services_->text().beginFrame(*windowAtlas_);
  scene_->paint(target_->painter());
  if (ui != nullptr) {
    ui->paint(target_->painter());
    ui->finishPaint();
  }
  paintMode(target_->painter());
  services_->text().uploadAtlas(*windowAtlas_);
  const auto t2 = Clock::now();
  const bool presented = target_->endFrame();
  const auto t3 = Clock::now();
  const bool atlasOverflowed = (ui != nullptr ? ui->consumeRepaint() : false) || services_->text().consumeAtlasOverflow(*windowAtlas_);
  if (presented) {
    redraw_ = atlasOverflowed;  // the atlas was reset: draw once more
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
    if (needsFrame()) frame();
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
  if (canDraw && needsFrame()) {
    frame();
    return true;
  }
  if (block) {
    widgets::UiContext* ui = activeUi();
    std::optional<uint64_t> wake;
    if (ui != nullptr) {
      ui->setTime(nowMs());
      wake = ui->msUntilTick();
    }
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
