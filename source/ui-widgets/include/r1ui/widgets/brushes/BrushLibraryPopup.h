// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: BrushLibraryPopup, the content of the brush library overlay (slices 5.21 and 5.22): a search field,
//   category chips, a virtualised grid of brush tiles (picture or icon, name, quick letter badge, favourite
//   star, outline for the active brush) with a Recent section, and a footer with the key hints and the
//   "pick on unique match" option; plus the small "Assign letter" popover and the tile context menu, both
//   drawn inside the popup.
// Why: owner requirement 2026-10-10: pressing B opens a library whose brushes are reached by typing their
//   letters. The popup is one widget that paints everything itself (like ThumbnailGrid) because it must own
//   every key while it is open, show 2000 tiles at no per-tile cost, and answer the keyboard-only paths
//   without moving focus between child widgets.
// Callers: BrushLibraryController (creates it in an overlay of the window that asked), the gallery,
//   tests. Calls: BrushLibraryModel (all decisions about names, letters and order), BrushThumbnailSource
//   (pictures), PaintContext.
// Keys while open (all of them belong to the popup; nothing reaches the application's commands):
//   a letter or digit narrows (type-to-pick) or adds to the search text; Backspace removes one character,
//   Ctrl+Backspace or Delete clears the text; Enter picks the highlighted tile; arrows, Home, End, PageUp and
//   PageDown move the highlight; Tab or Shift+Tab switches between type-to-pick and search-anywhere;
//   Ctrl+Left and Ctrl+Right change the category; Ctrl+F stars or unstars the highlighted brush; F2 opens the
//   Assign letter popover; Escape closes (first it closes the popover or menu if one is open); the key that
//   opened the library (as bound now) closes it when nothing is typed or when it is not a plain letter.
// Mouse: hover highlights, a click picks, a click on the star toggles the favourite, a right click opens
//   the tile menu, the wheel scrolls, chips and the mode and option controls are clickable; a click outside
//   the popup closes it (the overlay does that).
// Letters: the letters narrowing the list come from TextInput events (so any layout works); the popup
//   ignores the character that the opening key press produces and the repeats while that key is held.
// Lifetime: the popup keeps WidgetIds only; it unsubscribes from the model in onDetached. The model and
//   the hooks' targets must outlive the UiContext (the thumbnail source is shared).
// Failure behavior: hostile text is bounded (128 bytes typed), a stale highlight is clamped, paint never
//   throws for a missing icon or picture.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "r1ui/commands/brushes/BrushLibraryModel.h"
#include "r1ui/widgets/brushes/BrushThumbnails.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

namespace brushes {
class TileLayout;
}

struct BrushPopupHooks {
  std::function<void(const std::string& brushId)> onPick;  // the user chose a brush
  std::function<void()> onClose;                            // close without picking
  // True when this key press is the chord that opens the library, as bound now.
  std::function<bool(core::events::Key, uint8_t)> isOpenChord;
  std::string openChordText;                                // for the hint line ("B"); empty = none
  std::function<void()> onDetached;                         // the popup is being destroyed (its window may be gone)
};

struct BrushPopupOptions {
  double width = 680.0;
  double height = 480.0;
  commands::brushes::QueryMode mode = commands::brushes::QueryMode::TypeToPick;
  std::shared_ptr<BrushThumbnailSource> thumbnails;         // null = icons only
};

class BrushLibraryPopup : public WidgetObject {
 public:
  BrushLibraryPopup(commands::brushes::BrushLibraryModel& model, BrushPopupHooks hooks, BrushPopupOptions options = {});
  ~BrushLibraryPopup() override;

  const char* typeName() const override { return "BrushLibraryPopup"; }
  void onAttached() override;
  void onDetached() override;
  void onLayout() override;
  void paint(PaintContext& ctx) override;
  Cursor cursor() const override;
  std::string_view tooltipText() const override;
  std::string_view accessibleName() const override { return "Brush library"; }
  bool wantsTextInput() const override { return true; }

  void onKeyDown(Event& e) override;
  void onKeyUp(Event& e) override;
  void onTextInput(Event& e) override;
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onPointerWheel(Event& e) override;
  void onCaptureLost(Event& e) override;

  // ---- state, for the controller, the gallery and tests ----
  commands::brushes::QueryMode mode() const { return mode_; }
  void setMode(commands::brushes::QueryMode mode);
  const std::string& text() const { return text_; }
  // Replaces the typed text (type-to-pick keeps only letters and digits). Does not pick.
  void setText(std::string text);
  const std::string& category() const { return category_; }
  void setCategory(std::string category);
  const commands::brushes::QueryResult& result() const { return result_; }
  // The highlighted tile of result().tiles, or -1 when there is none.
  int highlight() const { return highlight_; }
  void setHighlight(int tile);
  std::optional<uint32_t> highlightedBrush() const;
  bool assigning() const { return assign_.open; }
  bool menuOpen() const { return menu_.open; }
  // What the Assign letter popover tells the user about the letter typed (empty when it is closed).
  const std::string& assignFeedback() const { return assign_.feedback; }
  // The line the footer shows right now (the notice of the last action, else the key hints).
  std::string hint() const;
  // Absolute logical rectangles of the parts (empty when not shown): a tile (empty when scrolled out of
  // the window), the star on it, the search field, the mode switch, the option, a chip (0 = All) and the grid.
  core::layout::RectD tileRect(size_t tile) const;
  core::layout::RectD starRect(size_t tile) const;
  core::layout::RectD searchRect() const;
  core::layout::RectD modeRect() const;
  core::layout::RectD optionRect() const;
  core::layout::RectD chipRect(size_t chip) const;
  core::layout::RectD gridRect() const;
  double scrollOffset() const { return scroll_; }
  size_t drawnTiles() const { return drawnTiles_; }
  size_t chipCount() const { return chips_.size(); }
  // Called by the controller after opening with a key: ignore the character that key produces (and its
  // repeats) until another key is pressed.
  void ignoreCharactersOf(char32_t letter);
  void pickHighlighted();
  // Opens the Assign letter popover or the tile menu for a brush, as F2 and a right click do.
  void openAssign(uint32_t brush);
  void showMenu(uint32_t brush);

 private:
  struct Chip {
    std::string label;
    std::string category;  // empty = All
    double x = 0.0, w = 0.0;
  };
  struct Assign {
    bool open = false;
    uint32_t brush = 0;
    char32_t candidate = 0;  // 0 = none typed yet
    bool clear = false;      // the user asked to remove the override
    std::string feedback;
  };
  struct Menu {
    bool open = false;
    uint32_t brush = 0;
    double x = 0.0, y = 0.0;  // relative to the popup
    int selected = 0;
    std::vector<std::string> labels;
    std::vector<int> actions;
  };

  // ---- query, layout and actions (BrushLibraryPopup.cpp) ----
  void refresh(bool keepHighlight);
  void relayout();
  void ensureVisible();
  void scrollTo(double offset);
  void rebuildChips(float scale);
  void ensureChipVisible();
  size_t activeChip() const;
  void typeCharacter(char32_t codePoint);
  void eraseCharacter();
  void clearText();
  void toggleMode();
  void selectChip(size_t chip);
  void stepChip(int direction);
  void moveHighlight(core::events::Key key);
  void toggleFavourite(uint32_t brush);
  void pickTile(size_t tile);
  void pickBrush(uint32_t brush);
  void close();
  double widthNow() const;
  double heightNow() const;
  double gridHeight() const;
  core::layout::RectD abs(double x, double y, double w, double h) const;
  int tileAt(double x, double y) const;  // absolute logical coordinates; -1 = none
  int chipAt(double x, double y) const;
  bool onScrollbar(double x, double y) const;

  // ---- keys and pointer (BrushLibraryPopupInput.cpp) ----
  bool handleControlKey(Event& e);
  void setHover(int tile, bool star);
  void dragScrollbar(double y);

  // ---- popovers (BrushLibraryPopupAssign.cpp) ----
  void assignKey(Event& e);
  void assignCharacter(char32_t codePoint);
  void applyAssign();
  void openMenu(uint32_t brush, double x, double y);
  void menuKey(Event& e);
  void runMenuAction(int action);
  void closeMenu();
  void menuPointer(double x, double y, bool press);
  core::layout::RectD menuRect() const;
  core::layout::RectD assignRect() const;

  // ---- painting (BrushLibraryPopupPaint.cpp) ----
  void paintHeader(PaintContext& ctx);
  void paintChips(PaintContext& ctx);
  void paintGrid(PaintContext& ctx);
  void paintTile(PaintContext& ctx, size_t tile, const core::layout::RectD& area);
  void paintFooter(PaintContext& ctx);
  void paintPopovers(PaintContext& ctx);
  void pumpThumbnails(PaintContext& ctx);

  commands::brushes::BrushLibraryModel& model_;
  BrushPopupHooks hooks_;
  BrushPopupOptions options_;
  commands::brushes::BrushLibraryModel::ListenerId listener_ = 0;

  commands::brushes::QueryMode mode_;
  std::string text_;
  std::string category_;
  commands::brushes::QueryResult result_;
  int highlight_ = -1;
  uint32_t highlightBrush_ = 0;
  bool hasHighlightBrush_ = false;
  double scroll_ = 0.0;
  double chipScroll_ = 0.0;
  float chipScale_ = 0.0f;
  std::vector<Chip> chips_;
  std::unique_ptr<brushes::TileLayout> layout_;
  double layoutWidth_ = -1.0;

  int hoverTile_ = -1;
  int hoverChip_ = -1;
  bool hoverMode_ = false;
  bool hoverOption_ = false;
  bool hoverStar_ = false;
  bool draggingScrollbar_ = false;
  double scrollbarGrab_ = 0.0;
  int pressTile_ = -1;
  bool pressStar_ = false;
  Assign assign_;
  Menu menu_;
  char32_t ignoreChar_ = 0;
  size_t drawnTiles_ = 0;
  bool animating_ = false;
  std::string notice_;
  mutable std::string tooltipScratch_;
  std::vector<uint64_t> wantedKeys_;
  std::vector<uint64_t> visibleKeys_;
};

}  // namespace r1ui::widgets
