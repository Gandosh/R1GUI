// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PreviewApp.h: construction, mode switching, key policy, event routing to
//   the title-bar scene, the active UiContext and the direct-drawn modes, the frame and the hooks of
//   the multi-window loop.
// Invariants: the loop (AppLoop) drives the main window and every native floating window; redraw_ is true whenever something outside the scene's and the active context's own
//   invalidation changed what must be shown (mode, theme, screens, sandbox, resize); frame() clears
//   it only after a frame was presented. The window's chrome layout is refreshed after every shell
//   layout pass. Only the active mode's context receives input and frames; the other context keeps
//   its widgets and catches up (layout, theme) when it is shown again.
// Callers: main.cpp, Bench.cpp, tests/preview.
#include "PreviewApp.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

#include "ComposedUtil.h"
#include "DriveStatus.h"
#include "r1ui/platform/Monitors.h"
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

// The toolkit's key code for a Windows virtual key (named keys only, like UiContext's own mapping).
r1ui::core::events::Key toolkitKey(uint32_t vk) {
  const bool named = vk == 8 || vk == 9 || vk == 13 || vk == 27 || vk == 32 || (vk >= 33 && vk <= 40) || vk == 45 || vk == 46 || (vk >= 48 && vk <= 57) ||
                     (vk >= 65 && vk <= 90) || (vk >= 112 && vk <= 123);
  return named ? static_cast<r1ui::core::events::Key>(vk) : r1ui::core::events::Key::Unknown;
}

uint8_t toolkitModifiers(const platform::Modifiers& m) {
  namespace mod = r1ui::core::events::Mod;
  return static_cast<uint8_t>((m.shift ? mod::kShift : 0) | (m.ctrl ? mod::kCtrl : 0) | (m.alt ? mod::kAlt : 0) | (m.meta ? mod::kMeta : 0));
}

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
  backend_ = std::make_unique<widgets::NativeFloatingBackend>(*window_, *device_, *services_);
  scene_ = std::make_unique<Scene>(services_->theme(), services_->text());
  scene_->setGlobalKeyHandler(this);
  swatches_ = std::make_unique<SwatchesView>(*tokens_);
  screens_ = std::make_unique<ScreensView>(*device_, paths_.references());
  sandbox_ = std::make_unique<DockSandbox>(*window_, *tokens_, paths_.layoutFile());

  syncViewport();
  scene_->layout();
  window_->setChromeLayout(scene_->chromeLayout());
  setMode(options.mode);

  // The loop also installs the live callbacks: while Windows runs its own move/size loop pumpEvents()
  // does not return, and every window calls back so all windows keep following.
  if (char* path = nullptr; _dupenv_s(&path, nullptr, "R1GUI_PREVIEW_STATUS") == 0 && path != nullptr) {
    statusPath_ = path;
    std::free(path);
  }
  widgets::AppLoopHooks hooks;
  hooks.processMain = [this](uint64_t) { processEvents(); };
  hooks.mainNeedsFrame = [this] { return needsFrame(); };
  hooks.renderMain = [this](uint64_t) { frame(); };
  hooks.mainMsUntilTick = [this](uint64_t) -> std::optional<uint64_t> {
    writeDriveStatus();  // after the native windows were served: what they did shows up without another event
    std::optional<uint64_t> wake;
    if (widgets::UiContext* ui = activeUi()) {
      ui->setTime(nowMs());
      wake = ui->msUntilTick();
    }
    if (const uint32_t retry = target_->retryDelayMs(); retry > 0) wake = wake ? std::min<uint64_t>(*wake, retry) : std::optional<uint64_t>(retry);
    if (editor_ && mode() == Mode::Editor) {
      if (const std::optional<uint64_t> save = editor_->msUntilTick()) wake = wake ? std::min(*wake, *save) : *save;
    }
    return wake;
  };
  hooks.quitRequested = [this] { return quit_; };
  loop_ = std::make_unique<widgets::AppLoop>(*window_, *backend_, std::move(hooks));
}

// Teardown order (docs/dev/native-windows.md section 5): the loop, then the Editor (it flushes its files
// and its dock closes the native windows), its context, and only then, by member order, the backend, the
// services, the device and the window.
PreviewApp::~PreviewApp() {
  loop_.reset();
  editor_.reset();
  editorUi_.reset();
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
  if (mode == Mode::Editor && !editorUi_) {
    editorUi_ = makeContext(content);
    editor::EditorHost host;
    host.dataRoot = editor::defaultDataRoot();
    host.setDarkTheme = [this](bool dark) { setDarkTheme(dark); };
    host.isDark = [this] { return scene_->theme().id() == r1ui::theme::ThemeId::Dark; };
    host.quit = [this] { quit_ = true; };
    host.showScreen = [this](int screen) {
      if (screen >= 0 && screen < kModeCount) setMode(static_cast<Mode>(screen));
    };
    host.screenNames = [] {
      std::vector<std::string> names;
      for (int i = 0; i < kModeCount; ++i) names.emplace_back(modeName(static_cast<Mode>(i)));
      return names;
    };
    host.monitors = [this] { return monitorSet(); };
    if (!brushTextures_) brushTextures_ = std::make_unique<widgets::GpuThumbnailTextures>(*device_);
    host.thumbnailSink = brushTextures_.get();
    editor_ = std::make_unique<editor::EditorApp>(*editorUi_, content, *backend_, std::move(host));
  } else if (mode == Mode::Gallery && !galleryUi_) {
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

// The connected monitors as the dock's layout loading wants them (logical units).
r1ui::dock::MonitorSet PreviewApp::monitorSet() {
  r1ui::dock::MonitorSet set;
  const r1ui::widgets::native::ScreenSpace& space = backend_->screenSpace();
  const std::vector<platform::MonitorInfo>& monitors = space.monitors();
  for (size_t i = 0; i < monitors.size(); ++i) {
    const int index = static_cast<int>(i);
    r1ui::dock::MonitorInfo info;
    info.name = monitors[i].name;
    info.bounds = space.logicalBounds(index);
    info.workArea = space.logicalWorkArea(index);
    info.scale = space.scaleOf(index);
    if (monitors[i].primary) set.primary = i;
    set.monitors.push_back(std::move(info));
  }
  return set;
}

widgets::UiContext* PreviewApp::activeUi() {
  switch (mode()) {
    case Mode::Editor: return editorUi_.get();
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
  if (mode() == Mode::Editor) return false;  // the Editor owns the plain keys (Tab is focus navigation there)
  widgets::UiContext* ui = activeUi();
  if (ui == nullptr) return true;
  return !ui->router().focused().valid() && ui->overlays().stack().empty();
}

// ---- Events ---------------------------------------------------------------------------------

void PreviewApp::syncViewport() {
  scene_->setViewport(window_->clientWidth(), window_->clientHeight(), window_->dpiScale());
  for (widgets::UiContext* ui : {editorUi_.get(), galleryUi_.get(), widgetsUi_.get()}) {
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
    case Mode::Editor:
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
        if (e.type == ET::KeyUp && editor_ && ui == editorUi_.get()) editor_->onKeyUp(toolkitKey(e.virtualKey), toolkitModifiers(e.modifiers));
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
        for (widgets::UiContext* each : {editorUi_.get(), galleryUi_.get(), widgetsUi_.get()}) {
          if (each != nullptr) each->handlePlatformEvent(e);
        }
        redraw_ = true;
        break;
      case ET::CloseRequested: break;  // the window closes itself (default policy)
      case ET::DisplayChanged: backend_->onDisplayChanged(); break;  // monitors changed: bring floating windows back
      default: scene_->handleEvent(e, now); break;
    }
  }
  if (ui != nullptr && ui->tick()) redraw_ = true;  // a due timer (tooltip, toast, menu delay) changed something
  if (editor_ && ui == editorUi_.get()) editor_->tick();  // layout auto-save is due on a timer, not on a frame
  writeDriveStatus();
  applyCursor();
}

// The scripted drive's coordinates (DriveStatus.h): rewritten only when the text changed.
void PreviewApp::writeDriveStatus() {
  if (statusPath_.empty() || mode() != Mode::Editor || !editor_) return;
  const std::string text = driveStatus(*this);
  if (text == lastStatus_) return;
  lastStatus_ = text;
  std::ofstream(statusPath_, std::ios::trunc) << text;
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
    case Mode::Editor:
    case Mode::Gallery:
    case Mode::Widgets: break;
  }
  painter.popClip();
}

bool PreviewApp::needsFrame() {
  const widgets::UiContext* ui = activeUi();
  if (target_->retryDelayMs() > 0) return false;  // the compositor stopped serving the window: wait out the back-off
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
    if (editor_ && ui == editorUi_.get()) editor_->update();
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
  writeDriveStatus();  // commands run from a native window change the main window's texts without an event of its own
  return presented;
}

bool PreviewApp::pumpMessages() {
  if (!window_->pumpEvents()) return false;
  processEvents();
  return !quit_;
}

bool PreviewApp::step(bool block, unsigned maxWaitMs) { return loop_->step(block, maxWaitMs); }

void PreviewApp::run() { loop_->run(); }

}  // namespace preview
