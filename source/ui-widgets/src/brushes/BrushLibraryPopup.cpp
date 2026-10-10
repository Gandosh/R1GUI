// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: BrushLibraryPopup's state side: construction, the model subscription, running the query,
//   the tile layout and scrolling, the category chips, typing, highlight movement and the actions
//   (pick, favourite, close). Keys and pointer are in BrushLibraryPopupInput.cpp, the Assign letter
//   popover and the tile menu in BrushLibraryPopupAssign.cpp, painting in BrushLibraryPopupPaint.cpp.
// Invariants: result_ is always the answer of model_.query for the current mode, text and category; the
//   highlight is -1 or a valid tile; scroll_ lies in [0, max scroll]; the popup never changes the tree
//   from paint; every callback to the host goes through hooks_ and is made after the popup has finished
//   its own state change (a pick may destroy the popup).
// Callers: BrushLibraryController, tests.
#include <algorithm>
#include <cmath>

#include "BrushPopupGeometry.h"
#include "r1ui/commands/brushes/BrushLetters.h"
#include "r1ui/widgets/brushes/BrushLibraryPopup.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace cb = commands::brushes;
using core::layout::RectD;
namespace bx = brushes;

namespace {

constexpr size_t kMaxTypedBytes = 128;

}  // namespace

BrushLibraryPopup::BrushLibraryPopup(cb::BrushLibraryModel& model, BrushPopupHooks hooks, BrushPopupOptions options)
    : model_(model), hooks_(std::move(hooks)), options_(std::move(options)), mode_(options_.mode), layout_(std::make_unique<bx::TileLayout>()) {
  options_.width = std::isfinite(options_.width) ? std::clamp(options_.width, 240.0, 2400.0) : 680.0;
  options_.height = std::isfinite(options_.height) ? std::clamp(options_.height, 200.0, 2400.0) : 480.0;
}

BrushLibraryPopup::~BrushLibraryPopup() = default;

void BrushLibraryPopup::onAttached() {
  core::layout::Style& s = style();
  s.width = core::layout::Length::px(options_.width);
  s.height = core::layout::Length::px(options_.height);
  s.flexShrink = 0.0;
  node().flags.clipsChildren = true;
  setFocusable(true);
  setWantsLayoutCallback(true);
  UiContext* context = &ui();
  const core::tree::WidgetId self = id();
  listener_ = model_.subscribe([context, self] {
    if (BrushLibraryPopup* popup = context->objectAs<BrushLibraryPopup>(self)) popup->refresh(true);
  });
  refresh(false);
}

void BrushLibraryPopup::onDetached() {
  if (hooks_.onDetached) {
    const auto detached = hooks_.onDetached;
    detached();
  }
  model_.unsubscribe(listener_);
  listener_ = 0;
  if (animating_) {
    ui().invalidator().cancelAnimation(id());
    animating_ = false;
  }
}

void BrushLibraryPopup::onLayout() {
  if (std::fabs(widthNow() - layoutWidth_) > 0.01) relayout();
}

// ---- geometry -----------------------------------------------------------------------------------

double BrushLibraryPopup::widthNow() const {
  const double w = attached() ? static_cast<double>(ui().absRect(id()).w) : 0.0;
  return w > 0.0 ? w : options_.width;
}

double BrushLibraryPopup::heightNow() const {
  const double h = attached() ? static_cast<double>(ui().absRect(id()).h) : 0.0;
  return h > 0.0 ? h : options_.height;
}

double BrushLibraryPopup::gridHeight() const { return std::max(0.0, bx::gridBox(widthNow(), heightNow()).h); }

RectD BrushLibraryPopup::abs(double x, double y, double w, double h) const {
  const core::layout::Rect r = ui().absRect(id());
  return {static_cast<double>(r.x) + x, static_cast<double>(r.y) + y, w, h};
}

RectD BrushLibraryPopup::gridRect() const {
  const bx::Box b = bx::gridBox(widthNow(), heightNow());
  return abs(b.x, b.y, b.w, b.h);
}

RectD BrushLibraryPopup::searchRect() const {
  const bx::Box b = bx::searchBox(widthNow());
  return abs(b.x, b.y, b.w, b.h);
}

RectD BrushLibraryPopup::modeRect() const {
  const bx::Box b = bx::modeBox(widthNow());
  return abs(b.x, b.y, b.w, b.h);
}

RectD BrushLibraryPopup::optionRect() const {
  const bx::Box b = bx::optionBox(widthNow(), heightNow());
  return abs(b.x, b.y, b.w, b.h);
}

RectD BrushLibraryPopup::chipRect(size_t chip) const {
  if (chip >= chips_.size()) return {};
  const bx::Box area = bx::chipsBox(widthNow());
  const double x = area.x + chips_[chip].x - chipScroll_;
  if (x + chips_[chip].w <= area.x || x >= area.x + area.w) return {};
  return abs(x, area.y, chips_[chip].w, area.h);
}

RectD BrushLibraryPopup::tileRect(size_t tile) const {
  if (tile >= result_.tiles.size()) return {};
  const bx::TileRect r = layout_->tileRect(static_cast<uint32_t>(tile));
  const bx::Box grid = bx::gridBox(widthNow(), heightNow());
  const double y = grid.y + r.y - scroll_;
  if (y + r.h <= grid.y || y >= grid.y + grid.h) return {};
  return abs(grid.x + r.x, y, r.w, r.h);
}

RectD BrushLibraryPopup::starRect(size_t tile) const {
  const RectD r = tileRect(tile);
  if (r.w <= 0.0) return {};
  const bx::TileRect thumb = layout_->thumbRect(static_cast<uint32_t>(tile));
  const bx::TileRect cell = layout_->tileRect(static_cast<uint32_t>(tile));
  return {r.x + (thumb.x - cell.x) + thumb.w - 22.0, r.y + (thumb.y - cell.y) + thumb.h - 22.0, 20.0, 20.0};
}

int BrushLibraryPopup::tileAt(double x, double y) const {
  const RectD grid = gridRect();
  if (x < grid.x || y < grid.y || x >= grid.x + grid.w || y >= grid.y + grid.h) return -1;
  const auto hit = layout_->hit(x - grid.x, y - grid.y + scroll_);
  return hit ? static_cast<int>(*hit) : -1;
}

int BrushLibraryPopup::chipAt(double x, double y) const {
  for (size_t i = 0; i < chips_.size(); ++i) {
    const RectD r = chipRect(i);
    if (r.w > 0.0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return static_cast<int>(i);
  }
  return -1;
}

bool BrushLibraryPopup::onScrollbar(double x, double y) const {
  const RectD grid = gridRect();
  if (layout_->contentHeight() <= grid.h) return false;
  return x >= grid.x + grid.w - 14.0 && x < grid.x + grid.w && y >= grid.y && y < grid.y + grid.h;
}

// ---- query --------------------------------------------------------------------------------------

// Runs the query again. `keepHighlight` follows the highlighted brush to its new tile (a favourite moved,
// the host changed the list); otherwise the highlight goes to the best match.
void BrushLibraryPopup::refresh(bool keepHighlight) {
  if (!category_.empty()) {
    const auto& categories = model_.categories();
    if (std::find(categories.begin(), categories.end(), category_) == categories.end()) category_.clear();
  }
  cb::QueryRequest request;
  request.mode = mode_;
  request.text = text_;
  request.category = category_;
  result_ = model_.query(request);

  int next = -1;
  if (!result_.tiles.empty()) {
    next = static_cast<int>(std::min<size_t>(result_.defaultHighlight, result_.tiles.size() - 1));
    if (keepHighlight && hasHighlightBrush_) {
      // Prefer the main-list tile of the brush over its Recent tile.
      for (size_t t = result_.tiles.size(); t-- > 0;) {
        if (result_.tiles[t].brush == highlightBrush_) {
          next = static_cast<int>(t);
          if (!result_.tiles[t].recent) break;
        }
      }
    }
  }
  highlight_ = next;
  hasHighlightBrush_ = next >= 0;
  if (next >= 0) highlightBrush_ = result_.tiles[static_cast<size_t>(next)].brush;
  if (hoverTile_ >= static_cast<int>(result_.tiles.size())) hoverTile_ = -1;
  if (attached()) {
    rebuildChips(ui().scale());  // the categories may have changed
    relayout();
    ensureVisible();
    requestPaint();
  }
}

void BrushLibraryPopup::relayout() {
  layoutWidth_ = widthNow();
  layout_->build(layoutWidth_, result_.recentCount, static_cast<uint32_t>(result_.tiles.size()), result_.recentCount > 0);
  scroll_ = std::clamp(scroll_, 0.0, layout_->maxScroll(gridHeight()));
}

void BrushLibraryPopup::ensureVisible() {
  if (highlight_ < 0) return;
  scroll_ = layout_->reveal(scroll_, gridHeight(), static_cast<uint32_t>(highlight_));
}

void BrushLibraryPopup::scrollTo(double offset) {
  const double next = std::clamp(std::isfinite(offset) ? offset : 0.0, 0.0, layout_->maxScroll(gridHeight()));
  if (next == scroll_) return;
  scroll_ = next;
  requestPaint();
}

// ---- chips --------------------------------------------------------------------------------------

void BrushLibraryPopup::rebuildChips(float scale) {
  chipScale_ = scale;
  chips_.clear();
  const float size = 11.0f * scale;
  const auto add = [&](std::string label, std::string category) {
    Chip chip;
    chip.w = std::ceil(static_cast<double>(ui().text().measure(label, size, 500)) / static_cast<double>(scale)) + 22.0;
    chip.x = chips_.empty() ? 0.0 : chips_.back().x + chips_.back().w + 6.0;
    chip.label = std::move(label);
    chip.category = std::move(category);
    chips_.push_back(std::move(chip));
  };
  add("All", {});
  for (const std::string& category : model_.categories()) add(category, category);
  ensureChipVisible();
}

size_t BrushLibraryPopup::activeChip() const {
  for (size_t i = 0; i < chips_.size(); ++i) {
    if (chips_[i].category == category_) return i;
  }
  return 0;
}

void BrushLibraryPopup::ensureChipVisible() {
  if (chips_.empty()) return;
  const bx::Box area = bx::chipsBox(widthNow());
  const Chip& chip = chips_[activeChip()];
  const double total = chips_.back().x + chips_.back().w;
  if (chip.x < chipScroll_) chipScroll_ = chip.x;
  else if (chip.x + chip.w > chipScroll_ + area.w) chipScroll_ = chip.x + chip.w - area.w;
  chipScroll_ = std::clamp(chipScroll_, 0.0, std::max(0.0, total - area.w));
}

void BrushLibraryPopup::selectChip(size_t chip) {
  if (chip >= chips_.size()) return;
  setCategory(chips_[chip].category);
}

void BrushLibraryPopup::stepChip(int direction) {
  if (chips_.empty()) return;
  const long next = std::clamp<long>(static_cast<long>(activeChip()) + direction, 0, static_cast<long>(chips_.size()) - 1);
  selectChip(static_cast<size_t>(next));
}

void BrushLibraryPopup::setCategory(std::string category) {
  if (!category.empty()) {
    const auto& categories = model_.categories();
    if (std::find(categories.begin(), categories.end(), category) == categories.end()) return;
  }
  if (category == category_) return;
  category_ = std::move(category);
  scroll_ = 0.0;
  notice_.clear();
  refresh(false);
  ensureChipVisible();
}

// ---- typing -------------------------------------------------------------------------------------

void BrushLibraryPopup::setMode(cb::QueryMode mode) {
  if (mode == mode_) return;
  mode_ = mode;
  notice_.clear();
  scroll_ = 0.0;
  refresh(false);
}

void BrushLibraryPopup::toggleMode() { setMode(mode_ == cb::QueryMode::TypeToPick ? cb::QueryMode::SearchAnywhere : cb::QueryMode::TypeToPick); }

void BrushLibraryPopup::setText(std::string text) {
  std::string kept;
  size_t pos = 0;
  while (pos < text.size() && kept.size() < kMaxTypedBytes) {
    const char32_t cp = cb::decodeUtf8(text, pos);
    if (mode_ == cb::QueryMode::TypeToPick ? cb::isKeyCharacter(cb::foldCodePoint(cp)) : (cp >= 0x20 && cp != 0x7f)) cb::appendUtf8(kept, cp);
  }
  if (kept == text_) return;
  text_ = std::move(kept);
  scroll_ = 0.0;
  refresh(false);
}

void BrushLibraryPopup::typeCharacter(char32_t cp) {
  notice_.clear();
  if (mode_ == cb::QueryMode::TypeToPick) {
    if (!cb::isKeyCharacter(cb::foldCodePoint(cp))) return;
    if (cb::keyOf(text_).size() >= cb::kMaxKeyLength) return;
  } else if (cp < 0x20 || cp == 0x7f) {
    return;
  }
  if (text_.size() + 4 > kMaxTypedBytes) return;
  cb::appendUtf8(text_, cp);
  scroll_ = 0.0;
  refresh(false);
  // The option: one brush left means it is picked now (never in the search mode).
  if (const auto unique = cb::pickOnUnique(result_, model_.pickOnUniqueOption())) pickBrush(*unique);
}

void BrushLibraryPopup::eraseCharacter() {
  notice_.clear();
  if (text_.empty()) return;
  size_t cut = text_.size() - 1;
  while (cut > 0 && (static_cast<unsigned char>(text_[cut]) & 0xC0u) == 0x80u) --cut;
  text_.resize(cut);
  scroll_ = 0.0;
  refresh(false);
}

void BrushLibraryPopup::clearText() {
  notice_.clear();
  if (text_.empty()) return;
  text_.clear();
  scroll_ = 0.0;
  refresh(false);
}

void BrushLibraryPopup::ignoreCharactersOf(char32_t letter) { ignoreChar_ = cb::foldCodePoint(letter); }

// ---- highlight and actions ----------------------------------------------------------------------

void BrushLibraryPopup::setHighlight(int tile) {
  const int value = tile >= 0 && tile < static_cast<int>(result_.tiles.size()) ? tile : -1;
  if (value == highlight_) return;
  highlight_ = value;
  hasHighlightBrush_ = value >= 0;
  if (value >= 0) highlightBrush_ = result_.tiles[static_cast<size_t>(value)].brush;
  requestPaint();
}

std::optional<uint32_t> BrushLibraryPopup::highlightedBrush() const {
  if (highlight_ < 0 || highlight_ >= static_cast<int>(result_.tiles.size())) return std::nullopt;
  return result_.tiles[static_cast<size_t>(highlight_)].brush;
}

void BrushLibraryPopup::moveHighlight(core::events::Key key) {
  using core::events::Key;
  if (result_.tiles.empty()) return;
  if (highlight_ < 0) {
    setHighlight(0);
    ensureVisible();
    return;
  }
  uint32_t tile = static_cast<uint32_t>(highlight_);
  const auto step = [&](bx::Move move, int times) {
    for (int i = 0; i < times; ++i) tile = layout_->move(tile, move);
  };
  switch (key) {
    case Key::Left: step(bx::Move::Left, 1); break;
    case Key::Right: step(bx::Move::Right, 1); break;
    case Key::Up: step(bx::Move::Up, 1); break;
    case Key::Down: step(bx::Move::Down, 1); break;
    case Key::Home: step(bx::Move::Home, 1); break;
    case Key::End: step(bx::Move::End, 1); break;
    case Key::PageUp: step(bx::Move::Up, std::max(1, static_cast<int>(gridHeight() / (bx::kCellH + bx::kGap)))); break;
    case Key::PageDown: step(bx::Move::Down, std::max(1, static_cast<int>(gridHeight() / (bx::kCellH + bx::kGap)))); break;
    default: return;
  }
  setHighlight(static_cast<int>(tile));
  ensureVisible();
}

void BrushLibraryPopup::toggleFavourite(uint32_t brush) {
  if (brush >= model_.size()) return;
  const std::string& brushId = model_.brushes()[brush].id;
  const bool now = !model_.isFavourite(brushId);
  if (model_.setFavourite(brushId, now)) notice_ = (now ? "Added " : "Removed ") + model_.brushes()[brush].name + (now ? " to the favourites." : " from the favourites.");
}

void BrushLibraryPopup::pickHighlighted() {
  if (highlight_ >= 0) pickTile(static_cast<size_t>(highlight_));
}

void BrushLibraryPopup::pickTile(size_t tile) {
  if (tile >= result_.tiles.size()) return;
  const cb::TileInfo& info = result_.tiles[tile];
  if (!info.enabled) {
    notice_ = model_.brushes()[info.brush].name + " is disabled and cannot be picked.";
    requestPaint();
    return;
  }
  pickBrush(info.brush);
}

void BrushLibraryPopup::pickBrush(uint32_t brush) {
  if (brush >= model_.size() || !hooks_.onPick) return;
  const std::string brushId = model_.brushes()[brush].id;  // copy: the callback may replace the list
  const auto pick = hooks_.onPick;
  pick(brushId);
}

void BrushLibraryPopup::close() {
  if (!hooks_.onClose) return;
  const auto closeHook = hooks_.onClose;
  closeHook();
}

}  // namespace r1ui::widgets
