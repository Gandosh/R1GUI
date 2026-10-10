// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: BrushLibraryController (slice 5.21), the object a host creates once to get a brush library: it
//   registers the command "brush.library" (default chord B, context global, configurable like every
//   command), opens the BrushLibraryPopup in an overlay of the window that asked, owns the open and close
//   rules, the shared picture cache, and hands the picked brush id to the host's handler.
// Why: owner requirement 2026-10-10: pressing B anywhere opens the brush library. The popup widget is
//   one window's overlay; the controller is the application-wide piece that knows which window asked,
//   keeps at most one library open, restores focus, and survives a window disappearing while it is open.
// Callers: the host (construction, tapKeys for every window's key handler, setThumbnails, handler).
//   Calls: CommandServices (registry, keymap), BrushLibraryModel, OverlayManager, BrushLibraryPopup.
//
// Key flow: the command is a normal Action in the "global" context, so the existing router decides when it
//   fires. A text field swallows plain letters before the router looks (the letter B types B). Focus on
//   a dock panel or the viewport reaches the global context as usual. While the popup is open it is a modal
//   overlay: the application's global key handler gets nothing and the popup, which holds the focus,
//   takes every key and character. Native floating windows have a UiContext each: the host wraps each
//   window's global key handler with tapKeys(), which tells the controller which window the key came from,
//   and the popup opens in that window (popups live in their own window's overlay layer and are bounded by
//   it; in a small floating window the popup shrinks to fit).
// Placement: centred on the pointer of that window, else on the window's centre; kept inside the window.
// Open and close: invoking the command while the library is open closes it (menu and toolbar toggles);
//   a pick, Escape, a click outside, losing the window's activation, or the controller's or the window's
//   destruction closes it. Focus returns to the widget that had it, unless a command moved it.
// Active brush: the host keeps it in the model (BrushLibraryModel::setActiveId) from its handler; the
//   controller only records the pick as a recent one.
// Lifetime: the controller must be destroyed before the command registry and the model. A UiContext may
//   be destroyed while the library is open in it; the controller notices through the popup's onDetached.
// Threading: UI thread only.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "r1ui/commands/brushes/BrushLibraryModel.h"
#include "r1ui/core/events/EventHandler.h"
#include "r1ui/widgets/brushes/BrushLibraryPopup.h"
#include "r1ui/widgets/commands/CommandServices.h"

namespace r1ui::widgets {

struct BrushLibraryOptions {
  std::string commandId = "brush.library";
  std::string label = "Brush library";
  std::string description = "Open the brush library: type the first letters of a brush name to pick it";
  std::string icon = "palette";
  std::string category = "Brushes";
  commands::ChordSequence defaultChord = commands::ChordSequence::single({static_cast<core::events::Key>('B'), 0, false});
  double width = 680.0;
  double height = 480.0;
  bool registerCommand = true;  // false: the host registers an equivalent command and calls open()/close()
};

class BrushLibraryController {
 public:
  using Handler = std::function<void(const std::string& brushId)>;

  // `primary` is the window the library opens in when the command is not run from a key press.
  BrushLibraryController(UiContext& primary, CommandServices services, commands::brushes::BrushLibraryModel& model, Handler onPick, BrushLibraryOptions options = {});
  ~BrushLibraryController();
  BrushLibraryController(const BrushLibraryController&) = delete;
  BrushLibraryController& operator=(const BrushLibraryController&) = delete;

  // Pictures for the tiles (both null = icons only). The provider and sink must outlive the controller.
  void setThumbnails(thumbs::ThumbnailProvider* provider, thumbs::ThumbnailTextureSink* sink);
  BrushThumbnailSource* thumbnails() const;

  // A global key handler for `ui` that records the window a key press comes from and then calls `inner`
  // (which may be null: keys are then not consumed). The returned object must live as long as it is
  // installed on `ui`; it may be kept after the window is gone (it holds no pointer into the window).
  std::unique_ptr<core::events::GlobalKeyHandler> tapKeys(UiContext& ui, core::events::GlobalKeyHandler* inner);

  // Opens in the window of the key press being handled, else in the primary window. False when it could
  // not be opened (no overlay, nothing to show is not an error).
  bool open();
  bool openIn(UiContext& ui);
  void close();
  bool isOpen() const;
  UiContext* window() const;                 // the window the library is open in (null when closed)
  BrushLibraryPopup* popup() const;          // null when closed
  const std::string& commandId() const;
  // Counts for tests: how many times the library opened and closed.
  size_t openCount() const;
  size_t closeCount() const;

 public:
  struct Impl;  // internal: shared with the popup's callbacks through weak pointers

 private:
  std::shared_ptr<Impl> impl_;
};

}  // namespace r1ui::widgets
