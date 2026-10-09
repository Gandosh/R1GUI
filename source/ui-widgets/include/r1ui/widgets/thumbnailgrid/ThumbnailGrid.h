// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ThumbnailGrid, the asset browser's item view (spec 12): a virtualised grid or list of tiles for
//   any number of items supplied by an AssetModel, with thumbnails delivered by a ThumbnailProvider
//   (type icon until the picture arrives; visible items first; bounded LRU texture cache), zoom
//   stops plus continuous zoom (Ctrl + wheel), selection and keyboard navigation (spec 08 and 12),
//   type-ahead, inline rename, a drag-out start callback, an incremental search filter, and the hooks
//   a host needs for back / forward / parent navigation and context menus.
// Why: a folder can hold 100 000 assets. Nothing is created per item: the widget draws only the
//   tiles of the visible window straight into the Painter, so scrolling costs the same for 100 items
//   and 100 000; pictures, filtering and sorting never block a frame.
// Callers: application code (the asset browser panel), the gallery, tests. Calls: GridLayout (maths),
//   ThumbnailCache (pictures), GridSupport (ordering, matching, type-ahead, history), PickerEntry
//   (the rename box), PaintContext (Painter, text, icons).
// Items and identity: an item is identified by its 64-bit key (stable across refreshes); positions in
//   the model change when it is re-sorted, keys do not. The selection, the cursor, the anchor and the
//   pending thumbnails are all kept by key, and after modelChanged() the selection keeps the keys that
//   still exist (spec 08 rule 71) and notifies once when it lost some.
// Model contract: count() and item(i, out) are called for visible items only (and by the filter in
//   slices of at most 15 ms per frame); item() must be cheap. The model is not owned and must outlive
//   the grid; call modelChanged() after its contents or order change.
// Selection rules (spec 08 45-53, 57-63, 66): press on an unselected item selects it alone; press on a
//   selected item keeps the selection (so a group can be dragged) and collapses to it on release when
//   no drag happened; Ctrl toggles; Shift adds the range from the anchor; a press on empty space
//   clears; arrows move by tile, Home / End, Page keys move by whole rows, Shift extends the range,
//   Ctrl moves the cursor and adds; Ctrl + A selects everything shown; Space toggles the cursor item
//   with Ctrl, otherwise previews; typing letters jumps to the next name with that prefix (2 s reset).
// Not implemented (see Goal/evidence/P4_g5_notes.md): the column (table) view mode, source-control
//   status strip, live thumbnails, saved filter sets, the 0.5 s slow-click rename (needs a widget timer
//   hook; F2 and the context action work), drag preview ghost (the host owns the drag after onDragStart).
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "r1ui/widgets/colorpicker/ColorModel.h"
#include "r1ui/widgets/colorpicker/PickerEntry.h"
#include "r1ui/widgets/runtime/WidgetObject.h"
#include "r1ui/widgets/thumbnailgrid/GridLayout.h"
#include "r1ui/widgets/thumbnailgrid/GridSupport.h"
#include "r1ui/widgets/thumbnailgrid/ThumbnailCache.h"

namespace r1ui::widgets {

struct GridItem {
  uint64_t key = 0;
  std::string name;
  std::string typeLabel;               // second line at larger sizes ("Texture", "Folder")
  std::string icon = "file";           // placeholder glyph (Lucide / custom icon name)
  color::Rgb typeColour{0.45, 0.45, 0.5};  // the strip at the bottom of the thumbnail
  bool folder = false;
  bool modified = false;               // unsaved changes marker
  bool readOnly = false;               // rename and delete are refused
};

class AssetModel {
 public:
  virtual ~AssetModel() = default;
  virtual size_t count() const = 0;
  // Fills `out` for item `index` (< count()).
  virtual void item(size_t index, GridItem& out) const = 0;
};

// A model over a vector, for tests and small lists.
class VectorAssetModel final : public AssetModel {
 public:
  std::vector<GridItem> items;
  size_t count() const override { return items.size(); }
  void item(size_t index, GridItem& out) const override {
    if (index < items.size()) out = items[index];
  }
};

enum class SortMode : uint8_t { Model, NameAscending, NameDescending };

struct GridContext {
  enum class Target : uint8_t { Empty, Item } target = Target::Empty;
  uint64_t key = 0;
  double x = 0.0;  // window coordinates (logical px)
  double y = 0.0;
};

struct DragRequest {
  std::vector<uint64_t> keys;  // every selected item, in shown order
  double x = 0.0;              // window coordinates of the press
  double y = 0.0;
};

class ThumbnailGrid : public WidgetObject {
 public:
  ThumbnailGrid();
  ~ThumbnailGrid() override;

  const char* typeName() const override { return "ThumbnailGrid"; }
  void onAttached() override;
  void onDetached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override;
  std::string_view tooltipText() const override;
  std::string_view accessibleName() const override;

  // ---- data ----
  void setModel(const AssetModel* model);
  void modelChanged();
  // Shows only the items the filter accepts (null shows all). The filter runs in slices of 15 ms per
  // frame; `filtering()` is true until it has seen every item.
  void setFilter(std::function<bool(const GridItem&)> filter);
  // Case-insensitive substring filter on the name; the matching letters are highlighted (rule 36).
  void setSearchText(std::string text);
  const std::string& searchText() const { return search_; }
  bool filtering() const { return filterRunning_; }
  void setSort(SortMode mode);
  size_t shownCount() const { return identity_ ? modelCount() : shown_.size(); }
  // Key of the item at shown position `i` (0 when out of range) and the shown position of a key.
  uint64_t keyAt(size_t shownIndex) const;
  size_t indexOfKey(uint64_t key) const;

  // ---- pictures ----
  void setProvider(thumbs::ThumbnailProvider* provider);
  void setTextureSink(thumbs::ThumbnailTextureSink* sink);
  void setCacheLimits(const thumbs::CacheLimits& limits);
  void invalidateThumbnail(uint64_t key);   // the asset changed: produce its picture again
  void notifyThumbnailReady(uint64_t key);  // a Pending picture can be fetched now
  size_t cachedThumbnails() const;
  size_t thumbnailRequests() const;
  bool hasThumbnail(uint64_t key) const;

  // ---- view ----
  void setViewMode(thumbs::ViewMode mode);
  thumbs::ViewMode viewMode() const { return mode_; }
  double zoom(thumbs::ViewMode mode) const { return zoom_[static_cast<size_t>(mode)]; }
  void setZoom(thumbs::ViewMode mode, double value);   // clamped; each mode remembers its own
  void stepZoom(int direction);                        // one stop up / down in the current mode
  double scrollOffset() const { return scroll_; }
  void setScrollOffset(double offset);
  void scrollToKey(uint64_t key, bool center);
  thumbs::Metrics metrics() const;
  thumbs::VisibleRange visibleRange() const;
  // Rectangle of a shown item in widget-local logical px (view coordinates).
  thumbs::Rect itemViewRect(size_t shownIndex) const;

  // ---- selection ----
  std::vector<uint64_t> selectedKeys() const;
  bool isSelected(uint64_t key) const { return selected_.count(key) != 0; }
  size_t selectionSize() const { return selected_.size(); }
  void setSelection(const std::vector<uint64_t>& keys);
  void clearSelection();
  void selectAll();
  uint64_t cursorKey() const { return cursor_; }

  // ---- rename ----
  void beginRename(uint64_t key);
  void cancelRename();
  bool renaming() const { return renaming_; }
  PickerEntry* renameEntry() const;

  // ---- navigation helper ----
  thumbs::NavigationHistory& history() { return history_; }

  // ---- events ----
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerWheel(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onDoubleClick(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;
  bool wantsTextInput() const override { return true; }
  void onTextInput(Event& e) override;
  void onFocusIn(Event& e) override;
  void onFocusOut(Event& e) override;
  void onLayout() override;

  // ---- callbacks ----
  std::function<void()> onSelectionChanged;
  std::function<void(const std::vector<uint64_t>& keys)> onActivate;     // Enter, Ctrl + E, double click
  std::function<void(const std::vector<uint64_t>& keys)> onPreview;      // Space
  std::function<void(const DragRequest&)> onDragStart;                   // press on a selected item and move past the threshold
  std::function<void(const GridContext&)> onContextMenu;
  std::function<void(const std::vector<uint64_t>& keys)> onDeleteRequested;
  // Rename: return true to accept; false keeps the box open with `error` as its tooltip.
  std::function<bool(uint64_t key, const std::string& newName, std::string& error)> onRename;
  std::function<void(thumbs::ViewMode mode, double zoom)> onZoomChanged;
  std::function<void()> onNavigateBack;     // mouse button 4
  std::function<void()> onNavigateForward;  // mouse button 5
  std::function<void()> onNavigateParent;   // Ctrl + Backspace
  std::function<void()> onNewFolder;        // Ctrl + Shift + N
  std::function<std::string(const GridItem&)> tooltipFor;  // null: name and type

 private:
  struct Press {
    bool active = false;
    uint64_t key = 0;
    size_t index = thumbs::kNone;
    bool wasSelected = false;
    uint8_t modifiers = 0;
    double x = 0.0;
    double y = 0.0;
    bool dragStarted = false;
    core::events::Button button = core::events::Button::None;
  };

  size_t modelCount() const;
  size_t modelIndex(size_t shown) const;
  void fetch(size_t shownIndex, GridItem& out) const;
  thumbs::Metrics computeMetrics() const;
  double viewportHeight() const;
  void clampScroll();
  void restartFilter();
  void pumpFilter();
  void finishSort();
  void pumpThumbnails(PaintContext& ctx, const thumbs::Metrics& m);
  uint32_t bucketFor(double scale) const;
  void rebuildKeyIndex() const;
  // `deferNotify`: called from paint, so the selection callback runs from a zero-delay timer instead.
  void rebuildSelectionAfterModelChange(bool deferNotify = false);

  // selection helpers
  void selectOnly(uint64_t key);
  void selectRange(size_t from, size_t to, bool additive);
  void moveCursor(size_t index, uint8_t modifiers);
  void commitSelection(std::unordered_set<uint64_t> next);
  std::vector<uint64_t> shownKeysBetween(size_t a, size_t b) const;
  size_t cursorIndex() const;
  void activate();
  void typeAheadJump(char32_t cp);
  void finishRename(bool commit, const std::string& text);
  void updateHover(double x, double y);
  void setScroll(double value);
  bool scrollbarThumb(thumbs::Rect& track, thumbs::Rect& thumb) const;

  // painting (ThumbnailGridPaint.cpp)
  void paintTile(PaintContext& ctx, const thumbs::Metrics& m, size_t shownIndex, const GridItem& item);
  void paintScrollbar(PaintContext& ctx, const thumbs::Metrics& m);

  const AssetModel* model_ = nullptr;
  std::function<bool(const GridItem&)> filter_;
  std::string search_;
  SortMode sort_ = SortMode::Model;
  bool identity_ = true;                 // shown order == model order (no filter, no sort)
  std::vector<uint32_t> shown_;          // model indices in shown order when !identity_
  bool filterRunning_ = false;
  size_t filterNext_ = 0;
  uint64_t shownVersion_ = 1;
  mutable std::unordered_map<uint64_t, uint32_t> keyIndex_;
  mutable uint64_t keyIndexVersion_ = 0;

  thumbs::ViewMode mode_ = thumbs::ViewMode::Grid;
  double zoom_[2] = {thumbs::kDefaultZoom, thumbs::kDefaultZoom};
  double scroll_ = 0.0;

  std::unordered_set<uint64_t> selected_;
  uint64_t anchor_ = 0;
  uint64_t cursor_ = 0;
  bool hasCursor_ = false;
  size_t hoverIndex_ = thumbs::kNone;
  double pointerX_ = 0.0;
  double pointerY_ = 0.0;
  bool pointerInside_ = false;
  Press press_;
  bool draggingScrollbar_ = false;
  double scrollbarGrab_ = 0.0;
  thumbs::TypeAhead typeAhead_;
  thumbs::NavigationHistory history_;

  // pictures
  std::unique_ptr<thumbs::ThumbnailCache> cache_;
  std::unique_ptr<thumbs::ThumbnailScheduler> scheduler_;
  thumbs::ThumbnailTextureSink* sink_ = nullptr;
  thumbs::ThumbnailProvider* provider_ = nullptr;
  thumbs::CacheLimits limits_;
  std::vector<uint64_t> wanted_;
  std::unordered_set<uint64_t> protectedKeys_;
  bool animating_ = false;
  bool selectionNotifyPending_ = false;
  // Wrapped label lines per (name, lines, width, size): bisecting a long name on every frame was both
  // slow and flooded the shaped-run cache. Cleared when full and whenever the label size changes.
  std::unordered_map<std::string, std::vector<std::string>> wrapCache_;

  // rename
  uint64_t renameKey_ = 0;
  bool renaming_ = false;
  bool renameInvalid_ = false;
  core::tree::WidgetId renameEntry_;

  mutable std::string tooltipScratch_;
  mutable GridItem scratchItem_;
  double lastWidth_ = 0.0;
};

}  // namespace r1ui::widgets
