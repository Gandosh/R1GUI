// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: construction of one native floating window (OS window, swapchain, context, frame, holder),
//   its event feed, viewport/chrome synchronisation and frame rendering (see NativeWindow.h).
// Invariants: the context's viewport always equals the OS client size and scale (set at construction
//   and on every Resized/DpiChanged event); the platform's chrome description always matches the
//   current client width and scale; a construction failure throws and leaves nothing behind (members
//   are RAII). Frames of different windows run one after the other, so the shared glyph atlas needs
//   no more than the context's own consumer.
// Callers: NativeFloatingBackend.
#include "NativeWindow.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "r1ui/platform/Utf.h"
#include "r1ui/widgets/gpu/GpuTextures.h"

namespace r1ui::widgets::native {

namespace layout = core::layout;

namespace {

platform::CursorShape platformCursor(Cursor cursor) {
  switch (cursor) {
    case Cursor::Pointer: return platform::CursorShape::Hand;
    case Cursor::Text: return platform::CursorShape::Text;
    case Cursor::ResizeHorizontal: return platform::CursorShape::ResizeHorizontal;
    case Cursor::ResizeVertical: return platform::CursorShape::ResizeVertical;
    case Cursor::ResizeNwSe: return platform::CursorShape::ResizeNwSe;
    case Cursor::ResizeNeSw: return platform::CursorShape::ResizeNeSw;
    case Cursor::Move: return platform::CursorShape::Move;
    case Cursor::NotAllowed: return platform::CursorShape::NotAllowed;
    case Cursor::Wait: return platform::CursorShape::Wait;
    case Cursor::Default: break;
  }
  return platform::CursorShape::Arrow;
}

int ceilToInt(double v) { return static_cast<int>(std::ceil(std::clamp(v, 1.0, 32000.0))); }

}  // namespace

NativeWindow::NativeWindow(const NativeWindowInit& init)
    : frame_(init.frame), services_(init.services) {
  const FloatRequest& request = *init.request;
  title = request.title;
  minContent = init.minContent;
  resizable = request.resizable;

  platform::WindowDesc desc;
  desc.title = platform::isValidUtf8(request.title) ? request.title : platform::replaceInvalidUtf8(request.title);
  desc.borderless = true;
  desc.toolWindow = true;
  desc.resizable = request.resizable;
  desc.owner = init.owner;
  desc.sizesAreLogical = true;
  desc.width = ceilToInt(init.contentSize.x + 2.0 * frame_.border);
  desc.height = ceilToInt(init.contentSize.y + frame_.titleHeight + frame_.border);
  desc.minSize = platform::Size{ceilToInt(init.minContent.x + 2.0 * frame_.border), ceilToInt(init.minContent.y + frame_.titleHeight + frame_.border)};
  desc.position = init.outerPosition;
  window_ = std::make_unique<platform::Window>(desc);
  window_->setCloseNeedsConfirmation(true);  // a close request is the user's wish; the host decides

  target_ = std::make_unique<render::WindowTarget>(*init.device, *window_);

  UiContextOptions options;
  platform::Window* w = window_.get();
  options.host.writeClipboard = [w](std::string_view text) { w->setClipboardText(text); };
  options.host.readClipboard = [w]() -> std::optional<std::string> {
    platform::ClipboardText clip = w->getClipboardText();
    if (clip.status != platform::ClipboardStatus::Ok) return std::nullopt;
    return std::move(clip.text);
  };
  ui_ = std::make_unique<UiContext>(*init.services, options);
  ui_->setFrameLoopRunning(true);
  ui_->setViewport(window_->clientWidth(), window_->clientHeight(), window_->dpiScale());
  frameWidget_ = ui_->create<NativeFrame>(ui_->root(), frame_).id();
  holder_ = ui_->create<NativeHolder>(frameWidget_).id();
  layout::Style& hs = ui_->object(holder_)->style();
  hs.inset[layout::kLeft] = layout::Length::px(frame_.border);
  hs.inset[layout::kTop] = layout::Length::px(frame_.titleHeight);
  hs.inset[layout::kRight] = layout::Length::px(frame_.border);
  hs.inset[layout::kBottom] = layout::Length::px(frame_.border);
  if (NativeFrame* f = ui_->objectAs<NativeFrame>(frameWidget_)) {
    f->setTitle(title);
    f->setResizable(resizable);
  }
  ui_->frame();
  syncChrome();
}

dock::Point NativeWindow::clientLogical() const {
  const double s = scale();
  return {static_cast<double>(window_->clientWidth()) / s, static_cast<double>(window_->clientHeight()) / s};
}

// ---- events -----------------------------------------------------------------------------------

WindowChanges NativeWindow::processEvents(uint64_t nowMs) {
  WindowChanges changes;
  ui_->setTime(nowMs);
  using ET = platform::EventType;
  for (const platform::Event& e : window_->takeEvents()) {
    switch (e.type) {
      case ET::Resized:
        syncViewport();
        changes.geometry = true;
        break;
      case ET::Moved: changes.geometry = true; break;
      case ET::DpiChanged:
        syncViewport();
        target_->invalidate();
        changes.dpi = changes.geometry = true;
        break;
      case ET::CloseRequested: changes.closeRequested = true; break;
      case ET::DisplayChanged: changes.displayChanged = true; break;
      case ET::MouseDown:
        changes.pressed = true;
        ui_->handlePlatformEvent(e);
        break;
      case ET::FocusGained:
      case ET::FocusLost:
        // The host hides the window of a dragged sole tab and the OS moves the focus away from it.
        // A context that heard "window inactive" would cancel the pointer interaction, i.e. the
        // drag the hiding was part of; a hidden window's activation is nobody's business.
        if (!shown) break;
        if (e.type == ET::FocusGained) changes.focused = true;
        ui_->handlePlatformEvent(e);
        break;
      default: ui_->handlePlatformEvent(e); break;
    }
  }
  if (ui_->tick()) redraw_ = true;  // a due timer (tooltip, menu delay) changed something
  applyCursor();
  return changes;
}

void NativeWindow::applyCursor() { window_->setCursor(platformCursor(ui_->cursor())); }

void NativeWindow::syncViewport() {
  ui_->setViewport(window_->clientWidth(), window_->clientHeight(), window_->dpiScale());
  if (NativeFrame* f = ui_->objectAs<NativeFrame>(frameWidget_)) f->setMaximized(window_->isMaximized());
  syncChrome();
  redraw_ = true;
}

void NativeWindow::syncChrome() {
  const NativeFrame* f = ui_->objectAs<NativeFrame>(frameWidget_);
  if (f == nullptr || window_->clientWidth() == 0) return;
  window_->setChromeLayout(f->chromeLayout(clientLogical().x, window_->dpiScale()));
}

void NativeWindow::applyTitle() {
  if (NativeFrame* f = ui_->objectAs<NativeFrame>(frameWidget_)) f->setTitle(title);
  window_->setTitle(platform::isValidUtf8(title) ? title : platform::replaceInvalidUtf8(title));
}

// ---- frames -----------------------------------------------------------------------------------

bool NativeWindow::needsFrame() const {
  if (!window_->isAlive() || !shown || !window_->isVisible() || window_->clientWidth() <= 0 || window_->clientHeight() <= 0) return false;
  return redraw_ || ui_->needsFrame();
}

bool NativeWindow::render(uint64_t nowMs) {
  if (!needsFrame()) return false;
  ui_->setTime(nowMs);
  redraw_ = false;
  const bool presented = renderFrame(*ui_, *target_, services_->color("panel"));
  if (presented) {
    ++framesPresented_;
  } else {
    redraw_ = true;  // the target was not ready (swapchain rebuilding): the damage is consumed, draw again
  }
  return presented;
}

std::optional<uint64_t> NativeWindow::msUntilTick(uint64_t nowMs) {
  ui_->setTime(nowMs);
  return ui_->msUntilTick();
}

}  // namespace r1ui::widgets::native
