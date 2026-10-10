// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: BrushLibraryController (BrushLibraryController.h): the command, the overlay lifecycle, the
//   key-source tap and the shared picture cache.
// Invariants: at most one library is open; openUi is non-null exactly while the popup exists (the popup's
//   onDetached and the overlay's onClosed both clear it); closing sets openUi to null before it asks the
//   overlay manager, so the manager's callback never counts a close twice; the command registered here is
//   removed in the destructor; every callback from the popup reaches the Impl through a weak pointer, so a
//   late callback after the controller is gone does nothing.
// Callers: hosts, tests.
#include "r1ui/widgets/brushes/BrushLibraryController.h"

#include <algorithm>

#include "r1ui/commands/brushes/BrushLetters.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace cb = commands::brushes;
using core::events::Key;

struct BrushLibraryController::Impl : std::enable_shared_from_this<Impl> {
  Impl(UiContext& primaryWindow, CommandServices s, cb::BrushLibraryModel& m, Handler h, BrushLibraryOptions o)
      : primary(&primaryWindow), services(s), model(m), onPick(std::move(h)), options(std::move(o)) {}

  UiContext* primary;
  CommandServices services;
  cb::BrushLibraryModel& model;
  Handler onPick;
  BrushLibraryOptions options;
  std::shared_ptr<BrushThumbnailSource> thumbnails;

  // The window and key of the key press being handled (set by the tap around the window's key handler).
  UiContext* keyWindow = nullptr;
  Key lastKey = Key::Unknown;
  uint8_t lastModifiers = 0;

  UiContext* openUi = nullptr;
  OverlayId overlay;
  core::tree::WidgetId popupId;
  size_t opens = 0;
  size_t closes = 0;
  bool registered = false;

  bool matchesOpenChord(Key key, uint8_t modifiers) const {
    const uint8_t mods = modifiers & commands::kAllModifiers;
    for (int slot = 0; slot < 2; ++slot) {
      const auto chord = services.keymap.effective(options.commandId, slot);
      if (chord && chord->count == 1 && chord->first().key == key && chord->first().modifiers == mods && !chord->first().onKeyUp) return true;
    }
    return false;
  }

  bool open(UiContext& ui, bool fromKey);
  void close();
  void popupClosedByOverlay() {
    if (openUi == nullptr) return;
    openUi = nullptr;
    ++closes;
  }
  void pick(const std::string& brushId) {
    close();
    model.noteUsed(brushId);
    const Handler handler = onPick;
    if (handler) handler(brushId);
  }
};

bool BrushLibraryController::Impl::open(UiContext& ui, bool fromKey) {
  close();  // one library at a time, whichever window it was in
  const double vw = ui.viewportWidth();
  const double vh = ui.viewportHeight();
  if (!(vw > 0.0) || !(vh > 0.0)) return false;
  const double width = std::clamp(options.width, 200.0, std::max(200.0, vw - 24.0));
  const double height = std::clamp(options.height, 160.0, std::max(160.0, vh - 24.0));
  const double cx = ui.pointerKnown() ? ui.pointerX() : vw * 0.5;
  const double cy = ui.pointerKnown() ? ui.pointerY() : vh * 0.5;
  const double x = std::clamp(cx - width * 0.5, 8.0, std::max(8.0, vw - width - 8.0));
  const double y = std::clamp(cy - height * 0.5, 8.0, std::max(8.0, vh - height - 8.0));

  OverlayOptions overlayOptions;
  overlayOptions.anchor = {static_cast<int32_t>(x), static_cast<int32_t>(y), 1, 1};
  overlayOptions.placement = Placement::Manual;
  overlayOptions.windowMargin = 8.0;
  overlayOptions.flip = false;
  overlayOptions.maxHeightFraction = 0.0;
  overlayOptions.modal = true;       // nothing behind the popup reacts, and the application's shortcuts are withheld
  overlayOptions.scrim = false;
  overlayOptions.trapFocus = false;  // Tab belongs to the popup (it switches the search mode)
  overlayOptions.dismissOnOutsidePress = true;
  overlayOptions.outsidePressPassesThrough = false;
  overlayOptions.dismissOnEscape = true;
  overlayOptions.dismissOnWindowDeactivate = true;
  overlayOptions.restoreFocus = true;
  overlayOptions.surface = OverlaySurface::Popover;
  const std::weak_ptr<Impl> weak = weak_from_this();
  overlayOptions.onClosed = [weak](DismissReason) {
    if (const auto self = weak.lock()) self->popupClosedByOverlay();
  };
  const OverlayHandle handle = ui.overlays().open(overlayOptions);
  if (!handle.valid()) return false;

  BrushPopupHooks hooks;
  hooks.onPick = [weak](const std::string& brushId) {
    if (const auto self = weak.lock()) self->pick(brushId);
  };
  hooks.onClose = [weak] {
    if (const auto self = weak.lock()) self->close();
  };
  hooks.isOpenChord = [weak](Key key, uint8_t modifiers) {
    const auto self = weak.lock();
    return self && self->matchesOpenChord(key, modifiers);
  };
  hooks.onDetached = [weak, ui = &ui] {
    const auto self = weak.lock();
    if (self && self->openUi == ui) {
      self->openUi = nullptr;
      ++self->closes;
    }
  };
  hooks.openChordText = services.keymap.displayText(options.commandId, true);
  BrushPopupOptions popupOptions;
  popupOptions.width = width;
  popupOptions.height = height;
  popupOptions.thumbnails = thumbnails;

  openUi = &ui;
  overlay = handle.id;
  try {
    BrushLibraryPopup& popup = ui.create<BrushLibraryPopup>(handle.host, model, std::move(hooks), popupOptions);
    popupId = popup.id();
    // The host stays invisible until the first frame places it, and an invisible widget cannot take the
    // focus: show it now (nothing is painted before the frame has placed it) so the keys that follow the
    // opening key press, even within the same input batch, already reach the popup.
    ui.invalidator().setVisible(handle.host, true);
    ui.focusWidget(popup.id());
    const unsigned k = static_cast<unsigned>(lastKey);
    const bool character = (k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9');
    if (fromKey && character && (lastModifiers & (core::events::Mod::kCtrl | core::events::Mod::kAlt | core::events::Mod::kMeta)) == 0) {
      popup.ignoreCharactersOf(static_cast<char32_t>(k));
    }
  } catch (...) {
    openUi = nullptr;
    ui.overlays().close(handle.id, DismissReason::Programmatic);
    throw;
  }
  ++opens;
  return true;
}

void BrushLibraryController::Impl::close() {
  if (openUi == nullptr) return;
  UiContext* ui = openUi;
  const OverlayId id = overlay;
  openUi = nullptr;
  popupId = {};
  ++closes;
  ui->overlays().close(id, DismissReason::Programmatic);
}

// ---- the key tap --------------------------------------------------------------------------------

namespace {

class KeyTap final : public core::events::GlobalKeyHandler {
 public:
  KeyTap(std::weak_ptr<BrushLibraryController::Impl>, UiContext*, core::events::GlobalKeyHandler*);
  bool onGlobalKey(const core::events::Event& event, core::events::Router& router) override;

 private:
  std::weak_ptr<BrushLibraryController::Impl> impl_;
  UiContext* ui_;
  core::events::GlobalKeyHandler* inner_;
};

}  // namespace

// ---- controller ---------------------------------------------------------------------------------

BrushLibraryController::BrushLibraryController(UiContext& primary, CommandServices services, cb::BrushLibraryModel& model, Handler onPick, BrushLibraryOptions options)
    : impl_(std::make_shared<Impl>(primary, services, model, std::move(onPick), std::move(options))) {
  if (!impl_->options.registerCommand) return;
  commands::CommandDef def;
  def.id = impl_->options.commandId;
  def.label = impl_->options.label;
  def.description = impl_->options.description;
  def.icon = impl_->options.icon;
  def.category = impl_->options.category;
  def.defaultChords = {impl_->options.defaultChord, commands::ChordSequence{}};
  const std::weak_ptr<Impl> weak = impl_;
  def.execute = [weak](const commands::ExecuteArgs& args) {
    const auto self = weak.lock();
    if (!self) return commands::ExecuteResult::notHandled();
    if (self->openUi != nullptr) {
      self->close();
      return commands::ExecuteResult::handled();
    }
    const bool fromKey = args.source == commands::ExecuteSource::Key && self->keyWindow != nullptr;
    UiContext& target = fromKey ? *self->keyWindow : *self->primary;
    return self->open(target, fromKey) ? commands::ExecuteResult::handled() : commands::ExecuteResult::refused("the window has no room for the brush library");
  };
  const commands::RegisterResult added = impl_->services.registry.add(std::move(def));
  impl_->registered = added.ok;
}

BrushLibraryController::~BrushLibraryController() {
  impl_->close();
  if (impl_->registered) impl_->services.registry.remove(impl_->options.commandId);
}

void BrushLibraryController::setThumbnails(thumbs::ThumbnailProvider* provider, thumbs::ThumbnailTextureSink* sink) {
  impl_->thumbnails = provider != nullptr && sink != nullptr ? std::make_shared<BrushThumbnailSource>(*provider, *sink) : nullptr;
}

BrushThumbnailSource* BrushLibraryController::thumbnails() const { return impl_->thumbnails.get(); }

std::unique_ptr<core::events::GlobalKeyHandler> BrushLibraryController::tapKeys(UiContext& ui, core::events::GlobalKeyHandler* inner) {
  return std::make_unique<KeyTap>(impl_, &ui, inner);
}

bool BrushLibraryController::open() { return impl_->open(impl_->keyWindow != nullptr ? *impl_->keyWindow : *impl_->primary, false); }
bool BrushLibraryController::openIn(UiContext& ui) { return impl_->open(ui, false); }
void BrushLibraryController::close() { impl_->close(); }
bool BrushLibraryController::isOpen() const { return impl_->openUi != nullptr; }
UiContext* BrushLibraryController::window() const { return impl_->openUi; }
const std::string& BrushLibraryController::commandId() const { return impl_->options.commandId; }
size_t BrushLibraryController::openCount() const { return impl_->opens; }
size_t BrushLibraryController::closeCount() const { return impl_->closes; }

BrushLibraryPopup* BrushLibraryController::popup() const {
  return impl_->openUi != nullptr && impl_->popupId.valid() ? impl_->openUi->objectAs<BrushLibraryPopup>(impl_->popupId) : nullptr;
}

namespace {

KeyTap::KeyTap(std::weak_ptr<BrushLibraryController::Impl> impl, UiContext* ui, core::events::GlobalKeyHandler* inner) : impl_(std::move(impl)), ui_(ui), inner_(inner) {}

bool KeyTap::onGlobalKey(const core::events::Event& event, core::events::Router& router) {
  const auto self = impl_.lock();
  if (self) {
    self->keyWindow = ui_;
    self->lastKey = event.key;
    self->lastModifiers = event.modifiers;
  }
  const bool used = inner_ != nullptr && inner_->onGlobalKey(event, router);
  if (self) self->keyWindow = nullptr;
  return used;
}

}  // namespace

}  // namespace r1ui::widgets
