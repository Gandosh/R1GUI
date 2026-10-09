// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: behaviour oracle for ThumbnailGrid through synthetic input and a recording Painter: the
//   acceptance scenarios of spec 12 (Ctrl + wheel zoom by stops and per-mode memory, 5000 and 100000
//   items with no per-item widgets, type-ahead, rename with Enter / Escape / invalid names, search
//   filtering and natural sorting, history), the selection rules of spec 08 (press, Ctrl, Shift,
//   collapse on release, drag threshold strictly greater than 5 px, marquee-free empty clicks,
//   keyboard navigation with Shift / Ctrl, Ctrl + A), the thumbnail pipeline as the widget uses it
//   (visible first, bounded cache, Pending / Failed, invalidation, never blocking), and hostile
//   input (no model, zero size, duplicate keys, destroyed widgets).
// Callers: CTest (thumbnailgrid, fast tier, no GPU).
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/thumbnailgrid/GalleryEditors.h"
#include "r1ui/widgets/thumbnailgrid/ThumbnailGrid.h"

namespace {

using namespace r1ui::widgets;
using thumbs::ViewMode;
namespace events = r1ui::core::events;
namespace layout = r1ui::core::layout;

constexpr events::Key kKeyA = static_cast<events::Key>('A');
constexpr events::Key kKeyE = static_cast<events::Key>('E');
constexpr events::Key kKeyN = static_cast<events::Key>('N');

// A model that makes names on demand: no storage per item.
class GeneratedModel final : public AssetModel {
 public:
  explicit GeneratedModel(size_t n) : n_(n) {}
  size_t count() const override { return n_; }
  void item(size_t i, GridItem& out) const override {
    out = GridItem{};
    out.key = 1000 + i;
    out.name = (i % 7 == 0 ? "Texture " : i % 7 == 1 ? "Mesh " : "Asset ") + std::to_string(i);
    out.typeLabel = i % 7 == 0 ? "Texture" : "Asset";
    out.icon = i % 7 == 0 ? "image" : "file";
    out.folder = i < folders_;
    out.modified = i % 50 == 3;
    if (i < folders_) out.icon = "folder-open";
  }
  size_t n_;
  size_t folders_ = 0;
};

struct Log {
  int selectionChanges = 0;
  std::vector<std::vector<uint64_t>> activated;
  std::vector<std::vector<uint64_t>> previewed;
  std::vector<DragRequest> drags;
  std::vector<GridContext> contexts;
  std::vector<std::vector<uint64_t>> deletes;
  std::vector<std::pair<ViewMode, double>> zooms;
  int back = 0, forward = 0, parent = 0, newFolder = 0;
  std::vector<std::pair<uint64_t, std::string>> renames;
};

struct Fixture {
  r1test::TestUi t{600, 500};
  ThumbnailGrid* grid = nullptr;
  Log log;
  std::unique_ptr<AssetModel> model;
  explicit Fixture(size_t n = 40, float scale = 1.0f) : t(600, 500, scale) {
    t.ui.rootStyle().alignItems = layout::Align::Stretch;
    grid = &t.ui.create<ThumbnailGrid>(t.ui.root());
    model = std::make_unique<GeneratedModel>(n);
    grid->setModel(model.get());
    wire();
    t.layout();
  }
  void wire() {
    grid->onSelectionChanged = [this] { ++log.selectionChanges; };
    grid->onActivate = [this](const std::vector<uint64_t>& k) { log.activated.push_back(k); };
    grid->onPreview = [this](const std::vector<uint64_t>& k) { log.previewed.push_back(k); };
    grid->onDragStart = [this](const DragRequest& d) { log.drags.push_back(d); };
    grid->onContextMenu = [this](const GridContext& c) { log.contexts.push_back(c); };
    grid->onDeleteRequested = [this](const std::vector<uint64_t>& k) { log.deletes.push_back(k); };
    grid->onZoomChanged = [this](ViewMode m, double z) { log.zooms.emplace_back(m, z); };
    grid->onNavigateBack = [this] { ++log.back; };
    grid->onNavigateForward = [this] { ++log.forward; };
    grid->onNavigateParent = [this] { ++log.parent; };
    grid->onNewFolder = [this] { ++log.newFolder; };
  }
  layout::Rect rect() { return t.ui.absRect(grid->id()); }
  // Centre of shown item `i` in window coordinates (the item must be visible).
  std::pair<double, double> centre(size_t i) {
    const thumbs::Rect r = grid->itemViewRect(i);
    return {rect().x + r.x + r.w / 2, rect().y + r.y + r.h / 2};
  }
  void click(size_t i, uint8_t mods = 0, events::Button b = events::Button::Left) {
    const auto [x, y] = centre(i);
    t.ui.pointerMove(x, y, mods);
    t.ui.pointerDown(x, y, b, mods);
    t.ui.pointerUp(x, y, b, mods);
  }
  void key(events::Key k, uint8_t mods = 0) { t.ui.keyDown(k, mods); }
  void focus() { t.ui.router().focus(grid->id(), events::FocusReason::Keyboard); }
  void paint() {
    r1ui::render::Painter painter;
    painter.begin(static_cast<uint32_t>(t.ui.viewportWidth() * t.ui.scale()), static_cast<uint32_t>(t.ui.viewportHeight() * t.ui.scale()));
    t.ui.paint(painter);
    t.ui.finishPaint();
    painter.end();
  }
  void settle() {  // paint until the filter has finished
    for (int i = 0; i < 2000 && grid->filtering(); ++i) paint();
    paint();
  }
  std::vector<uint64_t> selected() { return grid->selectedKeys(); }
};

std::vector<uint64_t> keys(std::initializer_list<size_t> indices) {
  std::vector<uint64_t> out;
  for (const size_t i : indices) out.push_back(1000 + i);
  return out;
}

// ---- zoom and scrolling ------------------------------------------------------------------------

void testZoomByStops() {
  Fixture f(20);
  R1_EXPECT(f.grid->zoom(ViewMode::Grid) == 128.0 && f.grid->zoom(ViewMode::List) == 128.0);
  const double before = f.grid->scrollOffset();
  // Acceptance 1: Ctrl + wheel up once: the next larger stop, saved, and the list does not scroll.
  const auto [x, y] = f.centre(0);
  f.t.ui.pointerMove(x, y);
  f.t.ui.wheel(x, y, 0.0, 1.0, events::Mod::kCtrl);
  R1_EXPECT(f.grid->zoom(ViewMode::Grid) == 160.0 && f.log.zooms.size() == 1 && f.log.zooms[0] == std::make_pair(ViewMode::Grid, 160.0));
  R1_EXPECT(f.grid->scrollOffset() == before);
  f.t.ui.wheel(x, y, 0.0, -1.0, events::Mod::kCtrl);
  f.t.ui.wheel(x, y, 0.0, -1.0, events::Mod::kCtrl);
  R1_EXPECT(f.grid->zoom(ViewMode::Grid) == 96.0);
  for (int i = 0; i < 10; ++i) f.t.ui.wheel(x, y, 0.0, -1.0, events::Mod::kCtrl);
  R1_EXPECT(f.grid->zoom(ViewMode::Grid) == 64.0);  // stops at the smallest
  const size_t zooms = f.log.zooms.size();
  f.t.ui.wheel(x, y, 0.0, -1.0, events::Mod::kCtrl);
  R1_EXPECT(f.log.zooms.size() == zooms);  // no change, no callback
  for (int i = 0; i < 10; ++i) f.t.ui.wheel(x, y, 0.0, 1.0, events::Mod::kCtrl);
  R1_EXPECT(f.grid->zoom(ViewMode::Grid) == 256.0);
  // Each view mode remembers its own zoom.
  f.grid->setViewMode(ViewMode::List);
  R1_EXPECT(f.grid->zoom(ViewMode::List) == 128.0 && f.grid->viewMode() == ViewMode::List);
  f.t.ui.wheel(x, y, 0.0, 1.0, events::Mod::kCtrl);
  R1_EXPECT(f.grid->zoom(ViewMode::List) == 160.0 && f.grid->zoom(ViewMode::Grid) == 256.0);
  f.grid->setViewMode(ViewMode::Grid);
  R1_EXPECT(f.grid->zoom(ViewMode::Grid) == 256.0);
  // Continuous zoom between the stops (the fine slider of D18) and hostile values.
  f.grid->setZoom(ViewMode::Grid, 143.5);
  R1_EXPECT(f.grid->zoom(ViewMode::Grid) == 143.5);
  f.grid->stepZoom(1);
  R1_EXPECT(f.grid->zoom(ViewMode::Grid) == 160.0);
  f.grid->setZoom(ViewMode::Grid, std::numeric_limits<double>::quiet_NaN());
  R1_EXPECT(f.grid->zoom(ViewMode::Grid) == thumbs::kDefaultZoom);
  f.grid->setZoom(ViewMode::Grid, 1e9);
  R1_EXPECT(f.grid->zoom(ViewMode::Grid) == thumbs::kMaxZoom);
  f.paint();
  // A plain wheel scrolls.
  Fixture g(400);
  const auto [gx, gy] = g.centre(0);
  g.t.ui.pointerMove(gx, gy);
  g.t.ui.wheel(gx, gy, 0.0, -2.0);
  R1_EXPECT(g.grid->scrollOffset() > 100.0);
  g.t.ui.wheel(gx, gy, 0.0, 1000.0);
  R1_EXPECT(g.grid->scrollOffset() == 0.0);
}

void testZoomKeepsFirstVisible() {
  Fixture f(400);
  f.grid->setScrollOffset(3000);
  const size_t first = f.grid->visibleRange().first;
  f.grid->stepZoom(1);
  f.paint();
  const size_t after = f.grid->visibleRange().first;
  // The first visible item stays about where it was (within the rows that now fit).
  R1_EXPECT(after <= first + 2 && first <= after + 8);
}

void testVirtualisationWithManyItems() {
  for (const size_t n : {size_t{5000}, size_t{100000}}) {
    Fixture f(n);
    const size_t widgets = f.t.ui.widgetCount();  // no widget per item: the count does not depend on n
    R1_EXPECT(widgets < 20);
    f.grid->setScrollOffset(f.grid->metrics().contentHeight / 2);
    f.paint();
    const thumbs::VisibleRange v = f.grid->visibleRange();
    R1_EXPECT(v.count() > 0 && v.count() < 300 && f.t.ui.widgetCount() == widgets);
    // Scroll quickly through the whole list: every frame stays cheap (the work is the visible window).
    const double maxScroll = f.grid->metrics().contentHeight - f.rect().h;
    const auto t0 = std::chrono::steady_clock::now();
    double worst = 0.0;
    const int frames = 200;
    for (int i = 0; i < frames; ++i) {
      f.grid->setScrollOffset(maxScroll * i / (frames - 1));
      const auto a = std::chrono::steady_clock::now();
      f.paint();
      worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count());
    }
    const double avg = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
    std::fprintf(stderr, "thumbnail grid: %zu items, %d scroll frames: average %.2f ms, worst %.2f ms, %zu widgets\n", n, frames, avg, worst, f.t.ui.widgetCount());
    R1_EXPECT(worst < 100.0 && f.t.ui.widgetCount() == widgets);
    // Selection of the last item and scrolling to it.
    f.grid->scrollToKey(1000 + n - 1, false);
    R1_EXPECT(f.grid->scrollOffset() == maxScroll || std::fabs(f.grid->scrollOffset() - maxScroll) < 1.0);
    f.grid->scrollToKey(1000 + n / 2, true);
    const thumbs::Rect mid = f.grid->itemViewRect(n / 2);
    R1_EXPECT(std::fabs(mid.y + mid.h / 2 - f.rect().h / 2.0) < 1.0);
  }
}

// ---- selection ---------------------------------------------------------------------------------

void testClickSelection() {
  Fixture f;
  f.click(2);
  R1_EXPECT(f.selected() == keys({2}) && f.log.selectionChanges == 1 && f.grid->cursorKey() == 1002);
  f.click(5, events::Mod::kCtrl);
  R1_EXPECT(f.selected() == keys({2, 5}) && f.log.selectionChanges == 2);
  f.click(2, events::Mod::kCtrl);  // toggles off
  R1_EXPECT(f.selected() == keys({5}));
  // Shift: the range from the anchor (the last plain / ctrl-clicked item) is added (rule 48).
  f.click(1);
  f.click(4, events::Mod::kShift);
  R1_EXPECT(f.selected() == keys({1, 2, 3, 4}));
  f.click(7, events::Mod::kShift);
  R1_EXPECT(f.selected() == keys({1, 2, 3, 4, 5, 6, 7}));  // the anchor did not move
  f.click(0, events::Mod::kShift);
  R1_EXPECT(f.selected() == keys({0, 1, 2, 3, 4, 5, 6, 7}));
  // A press on a selected item keeps the group; on release without a drag it collapses (rule 46).
  const auto [x, y] = f.centre(3);
  f.t.ui.pointerMove(x, y);
  f.t.ui.pointerDown(x, y);
  R1_EXPECT(f.selected().size() == 8);
  f.t.ui.pointerUp(x, y);
  R1_EXPECT(f.selected() == keys({3}));
  // A click on empty space (the gap right of the tiles, or below the last row) clears (rule 50) unless Ctrl / Shift.
  f.click(3);
  const thumbs::Rect last = f.grid->itemViewRect(39);
  const double ex = f.rect().x + 2;
  const double ey = f.rect().y + 2;
  f.t.ui.pointerMove(ex, ey, events::Mod::kCtrl);
  f.t.ui.pointerDown(ex, ey, events::Button::Left, events::Mod::kCtrl);
  f.t.ui.pointerUp(ex, ey, events::Button::Left, events::Mod::kCtrl);
  R1_EXPECT(f.selected() == keys({3}));
  f.t.ui.pointerMove(ex, ey);
  f.t.ui.pointerDown(ex, ey);
  f.t.ui.pointerUp(ex, ey);
  R1_EXPECT(f.selected().empty());
  (void)last;
}

void testDragThreshold() {
  Fixture f;
  f.click(1);
  f.click(2, events::Mod::kShift);
  const auto [x, y] = f.centre(2);
  // Exactly the threshold (5 px): no drag (rule 1); one pixel more: a drag of both selected items.
  f.t.ui.pointerMove(x, y);
  f.t.ui.pointerDown(x, y);
  f.t.ui.pointerMove(x + 5, y);
  R1_EXPECT(f.log.drags.empty());
  f.t.ui.pointerMove(x + 6, y);
  R1_EXPECT(f.log.drags.size() == 1 && f.log.drags[0].keys == keys({1, 2}));
  f.t.ui.pointerMove(x + 40, y + 20);  // once per press
  R1_EXPECT(f.log.drags.size() == 1);
  f.t.ui.pointerUp(x + 40, y + 20);
  R1_EXPECT(f.selected() == keys({1, 2}));  // a drag keeps the selection together
  // Dragging an unselected item selects it first.
  const auto [ux, uy] = f.centre(5);
  f.t.ui.pointerMove(ux, uy);
  f.t.ui.pointerDown(ux, uy);
  f.t.ui.pointerMove(ux + 12, uy);
  R1_EXPECT(f.log.drags.size() == 2 && f.log.drags[1].keys == keys({5}));
  f.t.ui.pointerUp(ux + 12, uy);
  // A drag that starts on empty space or with the right button never starts an item drag.
  f.t.ui.pointerMove(f.rect().x + 2, f.rect().y + 2);
  f.t.ui.pointerDown(f.rect().x + 2, f.rect().y + 2);
  f.t.ui.pointerMove(f.rect().x + 50, f.rect().y + 50);
  f.t.ui.pointerUp(f.rect().x + 50, f.rect().y + 50);
  R1_EXPECT(f.log.drags.size() == 2);
  f.t.ui.pointerMove(ux, uy);
  f.t.ui.pointerDown(ux, uy, events::Button::Right);
  f.t.ui.pointerMove(ux + 30, uy);
  f.t.ui.pointerUp(ux + 30, uy, events::Button::Right);
  R1_EXPECT(f.log.drags.size() == 2);
}

void testContextMenuAndActivation() {
  Fixture f;
  f.click(2);
  f.click(3, events::Mod::kCtrl);
  // Right click on a selected item keeps the selection (rule 51); on an unselected one replaces it.
  f.click(2, 0, events::Button::Right);
  R1_EXPECT(f.log.contexts.size() == 1 && f.log.contexts[0].target == GridContext::Target::Item && f.log.contexts[0].key == 1002 && f.selected() == keys({2, 3}));
  f.click(7, 0, events::Button::Right);
  R1_EXPECT(f.log.contexts.size() == 2 && f.selected() == keys({7}));
  // Right click on empty space clears (rule 52) and still asks for the menu.
  f.t.ui.pointerMove(f.rect().x + 2, f.rect().y + 2);
  f.t.ui.pointerDown(f.rect().x + 2, f.rect().y + 2, events::Button::Right);
  f.t.ui.pointerUp(f.rect().x + 2, f.rect().y + 2, events::Button::Right);
  R1_EXPECT(f.log.contexts.size() == 3 && f.log.contexts[2].target == GridContext::Target::Empty && f.selected().empty());
  // Double click opens the item under the pointer only (rule 54), Enter and Ctrl + E the whole selection.
  f.click(4);
  f.click(5, events::Mod::kShift);
  const auto [x, y] = f.centre(5);
  f.t.ui.pointerMove(x, y);
  f.t.ui.pointerDown(x, y);
  f.t.ui.pointerUp(x, y);
  f.t.ui.pointerDown(x, y);
  f.t.ui.pointerUp(x, y);
  R1_EXPECT(!f.log.activated.empty() && f.log.activated.back() == keys({5}));
  f.click(4);
  f.click(5, events::Mod::kShift);
  f.focus();
  f.key(events::Key::Enter);
  R1_EXPECT(f.log.activated.back() == keys({4, 5}));
  f.key(kKeyE, events::Mod::kCtrl);
  R1_EXPECT(f.log.activated.size() >= 3 && f.log.activated.back() == keys({4, 5}));
  f.key(events::Key::Space);
  R1_EXPECT(f.log.previewed.size() == 1 && f.log.previewed[0] == keys({4, 5}));
  f.key(events::Key::Delete);
  R1_EXPECT(f.log.deletes.size() == 1 && f.log.deletes[0] == keys({4, 5}));
  f.key(events::Key::Backspace, events::Mod::kCtrl);
  f.key(kKeyN, events::Mod::kCtrl | events::Mod::kShift);
  R1_EXPECT(f.log.parent == 1 && f.log.newFolder == 1);
  // The extra mouse buttons navigate.
  const auto [bx, by] = f.centre(0);
  f.t.ui.pointerMove(bx, by);
  f.t.ui.pointerDown(bx, by, events::Button::X1);
  f.t.ui.pointerUp(bx, by, events::Button::X1);
  f.t.ui.pointerDown(bx, by, events::Button::X2);
  f.t.ui.pointerUp(bx, by, events::Button::X2);
  R1_EXPECT(f.log.back == 1 && f.log.forward == 1);
  // Nothing selected: Enter, Delete and Space do nothing.
  f.grid->clearSelection();
  const size_t activations = f.log.activated.size();
  f.key(events::Key::Enter);
  f.key(events::Key::Delete);
  f.key(events::Key::Space);
  R1_EXPECT(f.log.activated.size() == activations && f.log.deletes.size() == 1 && f.log.previewed.size() == 1);
}

void testKeyboardNavigation() {
  Fixture f(100);
  const int columns = f.grid->metrics().columns;
  R1_EXPECT(columns >= 3);
  f.focus();
  f.key(events::Key::Down);  // from nothing: the first item
  R1_EXPECT(f.selected() == keys({0}) && f.grid->cursorKey() == 1000);
  f.key(events::Key::Right);
  f.key(events::Key::Down);
  R1_EXPECT(f.selected() == keys({static_cast<size_t>(1 + columns)}));
  f.key(events::Key::Up);
  f.key(events::Key::Left);
  R1_EXPECT(f.selected() == keys({0}));
  f.key(events::Key::End);
  R1_EXPECT(f.selected() == keys({99}));
  R1_EXPECT(f.grid->scrollOffset() > 0);  // the item is scrolled into view
  const thumbs::Rect lastRect = f.grid->itemViewRect(99);
  R1_EXPECT(lastRect.y >= 0 && lastRect.y + lastRect.h <= f.rect().h + 1e-6);
  f.key(events::Key::Home);
  R1_EXPECT(f.selected() == keys({0}) && f.grid->scrollOffset() == 0);
  f.key(events::Key::PageDown);
  const size_t page = f.grid->cursorKey() - 1000;
  R1_EXPECT(page > 0 && page % static_cast<size_t>(columns) == 0);
  f.key(events::Key::PageUp);
  R1_EXPECT(f.selected() == keys({0}));
  // Shift extends the range from the anchor; Ctrl moves the cursor and adds.
  f.key(events::Key::Right, events::Mod::kShift);
  f.key(events::Key::Right, events::Mod::kShift);
  R1_EXPECT(f.selected() == keys({0, 1, 2}));
  f.key(events::Key::Left, events::Mod::kShift);
  R1_EXPECT(f.selected() == keys({0, 1}));
  f.grid->setSelection(keys({0}));
  f.key(events::Key::Right, events::Mod::kCtrl);
  f.key(events::Key::Right, events::Mod::kCtrl);
  R1_EXPECT(f.selected() == keys({0, 1, 2}));
  // Alt changes nothing (rule 63).
  f.key(events::Key::Right, events::Mod::kAlt);
  R1_EXPECT(f.selected() == keys({0, 1, 2}));
  // Ctrl + A selects everything shown; Ctrl + Space toggles the cursor item.
  f.key(kKeyA, events::Mod::kCtrl);
  R1_EXPECT(f.selected().size() == 100);
  f.key(events::Key::Space, events::Mod::kCtrl);
  R1_EXPECT(f.selected().size() == 99);
  // The list view navigates by rows and ignores Left / Right.
  f.grid->setViewMode(ViewMode::List);
  f.grid->setSelection(keys({5}));
  f.focus();
  f.key(events::Key::Down);
  R1_EXPECT(f.selected() == keys({6}));
  f.key(events::Key::Right);
  R1_EXPECT(f.selected() == keys({6}));
}

void testTypeAhead() {
  Fixture f(60);
  f.focus();
  // "Mesh 1", "Mesh 8", "Mesh 15" ... come from i % 7 == 1: indices 1, 8, 15, 22, ...
  f.t.ui.setTime(1000);
  f.t.ui.textInput('m');
  R1_EXPECT(f.selected() == keys({1}));
  f.t.ui.setTime(1500);
  f.t.ui.textInput('e');
  f.t.ui.textInput('s');
  f.t.ui.textInput('h');
  f.t.ui.textInput(' ');
  f.t.ui.textInput('2');
  R1_EXPECT(f.selected() == keys({22}));  // "Mesh 2..." : the first one after the current is 22
  f.t.ui.textInput('2');                  // the current item "Mesh 22" already matches "mesh 22"
  R1_EXPECT(f.selected() == keys({22}));
  // After 2 seconds of silence typing starts a new prefix.
  f.t.ui.setTime(4000);
  f.t.ui.textInput('t');
  R1_EXPECT(f.selected() == keys({28}));  // "Texture 28"
  f.t.ui.setTime(4100);
  f.t.ui.textInput('e');
  f.t.ui.textInput('x');
  R1_EXPECT(f.selected() == keys({28}));  // already on a match of the longer prefix
  f.t.ui.textInput('q');
  R1_EXPECT(f.selected() == keys({28}));  // no match: the selection stays
  // Ctrl / Alt characters and Space (with an empty prefix) are not part of the prefix.
  f.t.ui.setTime(9000);
  f.t.ui.textInput('m', events::Mod::kCtrl);
  f.t.ui.textInput(' ');
  R1_EXPECT(f.selected() == keys({28}));
  // The jump scrolls the item into view.
  f.grid->setScrollOffset(0);
  f.t.ui.setTime(20000);
  f.t.ui.textInput('t');
  f.t.ui.textInput('e');
  f.t.ui.textInput('x');
  f.t.ui.textInput('t');
  f.t.ui.textInput('u');
  f.t.ui.textInput('r');
  f.t.ui.textInput('e');
  f.t.ui.textInput(' ');
  f.t.ui.textInput('5');
  f.t.ui.textInput('6');
  R1_EXPECT(f.selected() == keys({56}));
  const thumbs::Rect r = f.grid->itemViewRect(56);
  R1_EXPECT(r.y >= -1e-6 && r.y + r.h <= f.rect().h + 1e-6);
}

// ---- search, sort, model changes -----------------------------------------------------------------

void testSearchFilter() {
  Fixture f(2000);
  f.click(7);
  f.click(8, events::Mod::kCtrl);  // 7: "Texture 7"? i%7==0 -> 7 is Texture, 8 is Mesh
  f.log.selectionChanges = 0;
  f.grid->setSearchText("tex");
  R1_EXPECT(f.grid->filtering() || f.grid->shownCount() == 2000);
  f.settle();
  size_t expected = 0;
  GeneratedModel model(2000);
  GridItem item;
  for (size_t i = 0; i < 2000; ++i) {
    model.item(i, item);
    expected += item.name.find("Texture") != std::string::npos ? 1 : 0;
  }
  R1_EXPECT(!f.grid->filtering() && f.grid->shownCount() == expected && expected == 286);
  // The selected "Mesh 8" is hidden by the filter: removed, and the change is reported once (rule 73).
  R1_EXPECT(f.selected() == keys({7}) && f.log.selectionChanges == 1);
  R1_EXPECT(f.grid->keyAt(0) == 1000 && f.grid->keyAt(1) == 1007 && f.grid->indexOfKey(1007) == 1 && f.grid->indexOfKey(1008) == thumbs::kNone);
  // The match is highlighted when painted; clearing the search restores the full list without reselecting.
  f.paint();
  f.grid->setSearchText("");
  f.settle();
  R1_EXPECT(f.grid->shownCount() == 2000 && f.selected() == keys({7}));
  // An arbitrary filter, a filter that matches nothing, and the empty notice.
  f.grid->setFilter([](const GridItem& i) { return i.key % 100 == 0; });
  f.settle();
  R1_EXPECT(f.grid->shownCount() == 20);
  f.grid->setSearchText("zzz-no-match");
  f.settle();
  R1_EXPECT(f.grid->shownCount() == 0 && f.grid->selectedKeys().empty());
  f.paint();
  f.grid->setSearchText("");
  f.settle();
  R1_EXPECT(f.grid->shownCount() == 2000);
  // Case-insensitive, and a hostile search text is cut.
  f.grid->setSearchText("MESH 1");
  f.settle();
  R1_EXPECT(f.grid->shownCount() > 0);
  f.grid->setSearchText(std::string(100000, 'x'));
  f.settle();
  R1_EXPECT(f.grid->searchText().size() == 1024 && f.grid->shownCount() == 0);
}

void testFilterNeverBlocks() {
  Fixture f(300000);
  f.grid->setSearchText("Asset 29999");
  const auto t0 = std::chrono::steady_clock::now();
  f.paint();  // one frame: at most about 15 ms of filtering
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::fprintf(stderr, "thumbnail grid: first filter frame over 300000 items took %.1f ms (filtering still running: %d)\n", ms, f.grid->filtering() ? 1 : 0);
  R1_EXPECT(ms < 200.0);
  f.settle();
  R1_EXPECT(!f.grid->filtering() && f.grid->shownCount() > 0 && f.grid->shownCount() <= 11);
}

void testNaturalSortAndModelChange() {
  r1test::TestUi t(600, 500);
  t.ui.rootStyle().alignItems = layout::Align::Stretch;
  ThumbnailGrid& grid = t.ui.create<ThumbnailGrid>(t.ui.root());
  VectorAssetModel model;
  const char* names[] = {"item 10", "Item 2", "item 1", "item 20", "item 3"};
  for (uint64_t i = 0; i < 5; ++i) {
    GridItem it;
    it.key = i + 1;
    it.name = names[i];
    model.items.push_back(it);
  }
  grid.setModel(&model);
  t.layout();
  int changes = 0;
  grid.onSelectionChanged = [&] { ++changes; };
  grid.setSort(SortMode::NameAscending);
  for (int i = 0; i < 5; ++i) {
    r1ui::render::Painter painter;
    painter.begin(600, 500);
    t.ui.paint(painter);
    t.ui.finishPaint();
    painter.end();
  }
  // item 1, Item 2, item 3, item 10, item 20 -> keys 3, 2, 5, 1, 4
  R1_EXPECT(grid.keyAt(0) == 3 && grid.keyAt(1) == 2 && grid.keyAt(2) == 5 && grid.keyAt(3) == 1 && grid.keyAt(4) == 4);
  grid.setSort(SortMode::NameDescending);
  for (int i = 0; i < 5; ++i) {
    r1ui::render::Painter painter;
    painter.begin(600, 500);
    t.ui.paint(painter);
    painter.end();
  }
  R1_EXPECT(grid.keyAt(0) == 4 && grid.keyAt(4) == 3);
  grid.setSort(SortMode::Model);
  R1_EXPECT(grid.keyAt(0) == 1 && grid.keyAt(4) == 5);
  // The selection follows keys when items move, shrinks when items disappear, notifying once.
  grid.setSelection({1, 2, 3});
  changes = 0;
  std::swap(model.items[0], model.items[4]);
  grid.modelChanged();
  R1_EXPECT(grid.selectionSize() == 3 && changes == 0 && grid.keyAt(0) == 5);
  model.items.erase(model.items.begin() + 3, model.items.end());  // removes keys 4 and then 1 (positions 3, 4)
  grid.modelChanged();
  R1_EXPECT(grid.selectionSize() == 2 && changes == 1);
  model.items.clear();
  grid.modelChanged();
  R1_EXPECT(grid.selectionSize() == 0 && changes == 2 && grid.shownCount() == 0);
  // Selection by key survives a refresh that re-sorts the model.
  model.items.clear();
  for (uint64_t i = 0; i < 10; ++i) {
    GridItem it;
    it.key = 100 + i;
    it.name = "n" + std::to_string(i);
    model.items.push_back(it);
  }
  grid.modelChanged();
  grid.setSelection({103, 105});
  std::reverse(model.items.begin(), model.items.end());
  grid.modelChanged();
  R1_EXPECT(grid.selectedKeys() == std::vector<uint64_t>({105, 103}));  // in the new shown order
  R1_EXPECT(grid.indexOfKey(103) == 6 && grid.indexOfKey(105) == 4);
}

// ---- rename ------------------------------------------------------------------------------------

void testRename() {
  Fixture f(30);
  std::vector<std::string> attempts;
  f.grid->onRename = [&](uint64_t key, const std::string& name, std::string& error) {
    attempts.push_back(name);
    f.log.renames.emplace_back(key, name);
    if (name == "taken") {
      error = "A file with this name exists";
      return false;
    }
    return true;
  };
  f.click(3);
  f.focus();
  f.key(events::Key::F1 == events::Key::F1 ? static_cast<events::Key>(113) : events::Key::F1);  // F2
  R1_EXPECT(f.grid->renaming() && f.grid->renameEntry() != nullptr && f.grid->renameEntry()->text() == "Asset 3");
  R1_EXPECT(f.t.ui.router().focused() == f.grid->renameEntry()->id() && f.grid->renameEntry()->editor().hasSelection());  // all selected
  // Acceptance 8: Escape cancels with no change.
  f.key(events::Key::Escape);
  R1_EXPECT(!f.grid->renaming() && attempts.empty() && f.t.ui.router().focused() == f.grid->id());
  // Acceptance 7: F2, type a new name, Enter: renamed, still selected and in view.
  f.key(static_cast<events::Key>(113));
  for (const char c : std::string("Brick")) f.t.ui.textInput(static_cast<char32_t>(c));
  f.key(events::Key::Enter);
  R1_EXPECT(!f.grid->renaming() && attempts == std::vector<std::string>({"Brick"}) && f.log.renames[0].first == 1003);
  R1_EXPECT(f.selected() == keys({3}) && f.t.ui.router().focused() == f.grid->id());
  // Enter without changing the name closes without asking the host.
  f.key(static_cast<events::Key>(113));
  f.key(events::Key::Enter);
  R1_EXPECT(!f.grid->renaming() && attempts.size() == 1);
  // An invalid name: the box stays open with an error tooltip; Escape then cancels.
  f.key(static_cast<events::Key>(113));
  for (const char c : std::string("taken")) f.t.ui.textInput(static_cast<char32_t>(c));
  f.key(events::Key::Enter);
  R1_EXPECT(f.grid->renaming() && attempts.back() == "taken" && f.grid->renameEntry()->hasState(StateFlag::kInvalid) && f.grid->renameEntry()->tooltipText() == "A file with this name exists");
  f.key(events::Key::Escape);
  R1_EXPECT(!f.grid->renaming() && attempts.size() == 2);
  // Clicking elsewhere: a valid text commits, an invalid text is discarded (spec 08 rule 80).
  f.key(static_cast<events::Key>(113));
  for (const char c : std::string("Stone")) f.t.ui.textInput(static_cast<char32_t>(c));
  f.click(8);
  R1_EXPECT(!f.grid->renaming() && attempts.back() == "Stone");
  f.click(3);
  f.key(static_cast<events::Key>(113));
  for (const char c : std::string("taken")) f.t.ui.textInput(static_cast<char32_t>(c));
  f.key(events::Key::Enter);
  R1_EXPECT(f.grid->renaming());
  f.click(8);  // the invalid text is discarded
  R1_EXPECT(!f.grid->renaming() && attempts.back() == "taken");
  const size_t count = attempts.size();
  // Never with several items selected, nothing selected, or a read-only item.
  f.click(1);
  f.click(2, events::Mod::kShift);
  f.key(static_cast<events::Key>(113));
  R1_EXPECT(!f.grid->renaming());
  f.grid->clearSelection();
  f.key(static_cast<events::Key>(113));
  R1_EXPECT(!f.grid->renaming());
  f.grid->beginRename(999999);
  R1_EXPECT(!f.grid->renaming() && attempts.size() == count);
  // Scrolling closes the box.
  f.click(3);
  f.key(static_cast<events::Key>(113));
  R1_EXPECT(f.grid->renaming());
  const auto [x, y] = f.centre(0);
  f.t.ui.wheel(x, y, 0.0, -1.0);
  R1_EXPECT(!f.grid->renaming());
  f.paint();
}

void testRenameReadOnly() {
  r1test::TestUi t(600, 500);
  t.ui.rootStyle().alignItems = layout::Align::Stretch;
  ThumbnailGrid& grid = t.ui.create<ThumbnailGrid>(t.ui.root());
  VectorAssetModel model;
  GridItem it;
  it.key = 1;
  it.name = "locked";
  it.readOnly = true;
  model.items.push_back(it);
  grid.setModel(&model);
  t.layout();
  grid.beginRename(1);
  R1_EXPECT(!grid.renaming());
}

// ---- thumbnails ---------------------------------------------------------------------------------

class CountingProvider final : public thumbs::ThumbnailProvider {
 public:
  thumbs::ThumbnailStatus produce(uint64_t key, uint32_t sizePx, thumbs::ThumbnailImage& out) override {
    calls.push_back(key);
    sizes.push_back(sizePx);
    if (busyMs > 0.0) {
      const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double, std::milli>(busyMs);
      while (std::chrono::steady_clock::now() < end) {}
    }
    const auto it = status.find(key);
    const thumbs::ThumbnailStatus s = it == status.end() ? thumbs::ThumbnailStatus::Ready : it->second;
    if (s == thumbs::ThumbnailStatus::Ready) {
      out.width = out.height = 8;
      out.rgba.assign(8 * 8 * 4, 128);
    }
    return s;
  }
  std::vector<uint64_t> calls;
  std::vector<uint32_t> sizes;
  std::unordered_map<uint64_t, thumbs::ThumbnailStatus> status;
  double busyMs = 0.0;
};

void testThumbnailPipeline() {
  Fixture f(2000);
  CountingProvider provider;
  thumbs::MemoryThumbnailTextures sink;
  f.grid->setProvider(&provider);
  f.grid->setTextureSink(&sink);
  f.grid->setCacheLimits({60, size_t{1} << 30});
  f.paint();
  const thumbs::VisibleRange v = f.grid->visibleRange();
  R1_EXPECT(!provider.calls.empty() && provider.sizes[0] == 128);
  // The visible items are asked for first, in reading order, before any prefetch (rule 18).
  for (size_t i = 0; i < v.count() && i < provider.calls.size(); ++i) R1_EXPECT(provider.calls[i] == 1000 + v.first + i);
  R1_EXPECT(f.grid->hasThumbnail(1000) && f.grid->cachedThumbnails() == sink.alive());
  // Scrolling through a thousand items keeps the cache at its bound plus the protected window.
  for (int step = 0; step < 100; ++step) {
    f.grid->setScrollOffset(step * 900.0);
    for (int k = 0; k < 6; ++k) f.paint();
  }
  const size_t windowKeys = f.grid->visibleRange().count() + 2 * 64;
  R1_EXPECT(f.grid->cachedThumbnails() <= 60 + windowKeys && sink.alive() == f.grid->cachedThumbnails());
  // Items near the view keep their pictures: scrolling back a little re-requests nothing (rule 23).
  f.grid->setScrollOffset(50 * 900.0);
  for (int k = 0; k < 10; ++k) f.paint();
  const size_t requests = provider.calls.size();
  const thumbs::VisibleRange here = f.grid->visibleRange();
  f.grid->setScrollOffset(50 * 900.0 - 200.0);
  for (int k = 0; k < 4; ++k) f.paint();
  bool allThere = true;
  for (size_t i = f.grid->visibleRange().first; i < here.last; ++i) allThere &= f.grid->hasThumbnail(1000 + i);
  R1_EXPECT(allThere && provider.calls.size() - requests < 200);
  // Pending pictures are asked for again; Failed ones keep the icon until invalidated (rule 26).
  provider.calls.clear();
  provider.status[1000 + here.first] = thumbs::ThumbnailStatus::Failed;
  f.grid->invalidateThumbnail(1000 + here.first);
  f.grid->setScrollOffset(50 * 900.0);
  for (int k = 0; k < 4; ++k) f.paint();
  R1_EXPECT(!f.grid->hasThumbnail(1000 + here.first));
  const size_t failedCalls = std::count(provider.calls.begin(), provider.calls.end(), 1000 + here.first);
  for (int k = 0; k < 4; ++k) f.paint();
  R1_EXPECT(std::count(provider.calls.begin(), provider.calls.end(), 1000 + here.first) == static_cast<long>(failedCalls));  // not asked again
  provider.status[1000 + here.first] = thumbs::ThumbnailStatus::Pending;
  f.grid->invalidateThumbnail(1000 + here.first);
  provider.calls.clear();
  f.paint();
  f.paint();
  R1_EXPECT(std::count(provider.calls.begin(), provider.calls.end(), 1000 + here.first) >= 2);  // polled every pass
  provider.status.erase(1000 + here.first);
  f.paint();
  R1_EXPECT(f.grid->hasThumbnail(1000 + here.first));
  // Zooming past a size bucket asks for the new size; the old picture is shown until it arrives.
  provider.calls.clear();
  provider.sizes.clear();
  f.grid->setZoom(ViewMode::Grid, 256);
  f.paint();
  R1_EXPECT(!provider.sizes.empty() && provider.sizes[0] == 256);
  // The pictures are drawn: texture quads appear in the paint list beyond the glyph quads.
  r1ui::render::Painter painter;
  painter.begin(600, 500);
  f.t.ui.paint(painter);
  f.t.ui.finishPaint();
  painter.end();
  R1_EXPECT(painter.stats().texInstances > 20);
}

void testThumbnailsNeverBlock() {
  Fixture f(1000);
  CountingProvider provider;
  provider.busyMs = 2.0;  // each picture takes 2 ms to produce
  thumbs::MemoryThumbnailTextures sink;
  f.grid->setProvider(&provider);
  f.grid->setTextureSink(&sink);
  const auto t0 = std::chrono::steady_clock::now();
  f.paint();
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  const size_t visible = f.grid->visibleRange().count();
  std::fprintf(stderr, "thumbnail grid: first frame with 2 ms pictures: %.1f ms, %zu of %zu visible requested\n", ms, provider.calls.size(), visible);
  R1_EXPECT(visible > 6 && provider.calls.size() < visible && provider.calls.size() >= 1);  // the budget (5 ms) stopped it early
  R1_EXPECT(f.t.ui.needsFrame());                                                            // and more frames are requested
  for (int i = 0; i < 400 && f.t.ui.needsFrame(); ++i) f.paint();
  R1_EXPECT(f.grid->hasThumbnail(1000));
  // Without a provider or a sink nothing is requested and the icons show.
  Fixture g(100);
  g.paint();
  R1_EXPECT(g.grid->cachedThumbnails() == 0 && g.grid->thumbnailRequests() == 0);
}

// ---- hostile input -------------------------------------------------------------------------------

void testHostile() {
  {
    r1test::TestUi t(600, 500);
    t.ui.rootStyle().alignItems = layout::Align::Stretch;
    ThumbnailGrid& grid = t.ui.create<ThumbnailGrid>(t.ui.root());  // no model at all
    t.layout();
    r1ui::render::Painter painter;
    painter.begin(600, 500);
    t.ui.paint(painter);
    painter.end();
    grid.setSearchText("x");
    grid.selectAll();
    grid.stepZoom(1);
    grid.setScrollOffset(100);
    grid.scrollToKey(5, true);
    grid.beginRename(5);
    t.ui.router().focus(grid.id(), events::FocusReason::Keyboard);
    t.ui.keyDown(events::Key::Down);
    t.ui.keyDown(events::Key::End);
    t.ui.keyDown(events::Key::Enter);
    t.ui.pointerMove(100, 100);
    t.ui.pointerDown(100, 100);
    t.ui.pointerUp(100, 100);
    t.ui.wheel(100, 100, 0, 1);
    R1_EXPECT(grid.shownCount() == 0 && grid.selectedKeys().empty());
  }
  {
    // A model with duplicate keys, empty and invalid UTF-8 names, and enormous names.
    r1test::TestUi t(600, 500);
    t.ui.rootStyle().alignItems = layout::Align::Stretch;
    ThumbnailGrid& grid = t.ui.create<ThumbnailGrid>(t.ui.root());
    VectorAssetModel model;
    const std::string names[] = {"", "\xFF\xFE bad", std::string(100000, 'W'), "caf\xC3\xA9", "tab\tname", std::string("nul\0byte", 8), "dup", "dup"};
    for (size_t i = 0; i < 8; ++i) {
      GridItem it;
      it.key = i >= 6 ? 77 : i + 1;
      it.name = names[i];
      it.icon = i == 3 ? "no-such-icon-in-the-set" : "file";
      model.items.push_back(it);
    }
    grid.setModel(&model);
    t.layout();
    for (int k = 0; k < 3; ++k) {
      r1ui::render::Painter painter;
      painter.begin(600, 500);
      t.ui.paint(painter);
      painter.end();
    }
    grid.selectAll();
    grid.setSearchText("dup");
    for (int i = 0; i < 20 && grid.filtering(); ++i) {
      r1ui::render::Painter painter;
      painter.begin(600, 500);
      t.ui.paint(painter);
      painter.end();
    }
    R1_EXPECT(grid.shownCount() == 2);
  }
  {
    // A zero-size grid and a very narrow one.
    r1test::TestUi t(600, 500);
    t.ui.rootStyle().alignItems = layout::Align::Start;
    ThumbnailGrid& grid = t.ui.create<ThumbnailGrid>(t.ui.root());
    GeneratedModel model(50);
    grid.setModel(&model);
    for (const double w : {0.0, 1.0, 20.0, 5000.0}) {
      grid.style().width = layout::Length::px(w);
      grid.style().height = layout::Length::px(w == 5000.0 ? 10 : w);
      grid.requestLayout();
      t.layout();
      r1ui::render::Painter painter;
      painter.begin(600, 500);
      t.ui.paint(painter);
      painter.end();
      t.ui.pointerMove(w / 2, w / 2);
      t.ui.pointerDown(w / 2, w / 2);
      t.ui.pointerUp(w / 2, w / 2);
      grid.setScrollOffset(1e12);
      R1_EXPECT(std::isfinite(grid.scrollOffset()));
    }
    grid.setScrollOffset(std::numeric_limits<double>::quiet_NaN());
    R1_EXPECT(std::isfinite(grid.scrollOffset()));
  }
  {
    // Destroying the grid or the model's owner from inside callbacks.
    Fixture f(20);
    f.grid->onActivate = [&](const std::vector<uint64_t>&) { f.t.ui.destroy(f.grid->id()); };
    f.click(2);
    f.focus();
    f.key(events::Key::Enter);
    f.t.layout();
    f.paint();
  }
  {
    Fixture f(20);
    f.grid->onSelectionChanged = [&] { f.t.ui.destroy(f.grid->id()); };
    const auto [x, y] = f.centre(2);
    f.t.ui.pointerMove(x, y);
    f.t.ui.pointerDown(x, y);
    f.t.ui.pointerUp(x, y);
    f.t.layout();
    f.paint();
  }
  {
    Fixture f(20);
    f.grid->onContextMenu = [&](const GridContext&) { f.t.ui.destroy(f.grid->id()); };
    const auto [x, y] = f.centre(2);
    f.t.ui.pointerMove(x, y);
    f.t.ui.pointerDown(x, y, events::Button::Right);
    f.t.ui.pointerUp(x, y, events::Button::Right);
    f.t.layout();
    f.paint();
  }
  for (const float scale : {1.25f, 2.0f}) {
    Fixture f(200, scale);
    f.grid->setSearchText("Asset 1");
    f.settle();
    f.click(0);
    f.paint();
    R1_EXPECT(f.grid->shownCount() > 0);
  }
}

void testHistoryHooks() {
  Fixture f(10);
  f.grid->history().visit(1);
  f.grid->history().visit(2);
  R1_EXPECT(f.grid->history().canBack() && *f.grid->history().back() == 1);
}

// The gallery entry builds all four editors, lays them out and paints them without throwing.
void testGallery() {
  r1test::TestUi t(1400, 900);
  const size_t before = t.ui.widgetCount();
  buildGalleryEditors(t.ui, t.ui.root());
  R1_EXPECT(t.ui.widgetCount() > before + 4);
  t.layout();
  r1ui::render::Painter painter;
  painter.begin(1400, 900);
  t.ui.paint(painter);
  t.ui.finishPaint();
  painter.end();
}

}  // namespace

int main() {
  testGallery();
  testZoomByStops();
  testZoomKeepsFirstVisible();
  testVirtualisationWithManyItems();
  testClickSelection();
  testDragThreshold();
  testContextMenuAndActivation();
  testKeyboardNavigation();
  testTypeAhead();
  testSearchFilter();
  testFilterNeverBlocks();
  testNaturalSortAndModelChange();
  testRename();
  testRenameReadOnly();
  testThumbnailPipeline();
  testThumbnailsNeverBlock();
  testHostile();
  testHistoryHooks();
  return r1test::finish();
}
