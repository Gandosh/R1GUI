// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ThumbnailGrid.h except painting (ThumbnailGridPaint.cpp) and input
//   (ThumbnailGridInput.cpp): the model view (identity or filtered and sorted order, incremental
//   filtering), the key index, scrolling, zoom, thumbnail scheduling and selection storage.
// Invariants: shown order is the model order when identity_ is set, otherwise shown_ holds model
//   indices; shownVersion_ changes whenever the shown order changes (the key index is rebuilt lazily
//   against it); the selection holds keys, never positions; the filter only appends to shown_ (never
//   reorders it) until it finishes, so the visible window is stable while it runs; callbacks may
//   destroy the widget.
// Callers: application code, the gallery, tests.
#include "r1ui/widgets/thumbnailgrid/ThumbnailGrid.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

namespace events = core::events;
using thumbs::kNone;

constexpr double kFilterBudgetMs = 15.0;
constexpr size_t kPrefetchItems = 64;

double steadyMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

}  // namespace

ThumbnailGrid::ThumbnailGrid() = default;
ThumbnailGrid::~ThumbnailGrid() = default;

void ThumbnailGrid::onAttached() {
  style().flexGrow = 1.0;
  node().flags.clipsChildren = true;
  setFocusable(true);
  setWantsLayoutCallback(true);
}

void ThumbnailGrid::onDetached() { cancelRename(); }

void ThumbnailGrid::onLayout() {
  const double w = ui().absRect(id()).w;
  if (w != lastWidth_) {
    lastWidth_ = w;
    clampScroll();
  }
}

std::string_view ThumbnailGrid::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view("Asset browser") : WidgetObject::accessibleName();
}

// ---- model view -------------------------------------------------------------------------------

size_t ThumbnailGrid::modelCount() const { return model_ != nullptr ? std::min(model_->count(), thumbs::kMaxItems) : 0; }

size_t ThumbnailGrid::modelIndex(size_t shown) const { return identity_ ? shown : static_cast<size_t>(shown_[shown]); }

void ThumbnailGrid::fetch(size_t shownIndex, GridItem& out) const {
  out = GridItem{};
  if (model_ == nullptr || shownIndex >= shownCount()) return;
  model_->item(modelIndex(shownIndex), out);
}

uint64_t ThumbnailGrid::keyAt(size_t shownIndex) const {
  if (shownIndex >= shownCount()) return 0;
  fetch(shownIndex, scratchItem_);
  return scratchItem_.key;
}

void ThumbnailGrid::rebuildKeyIndex() const {
  if (keyIndexVersion_ == shownVersion_) return;
  keyIndex_.clear();
  const size_t n = shownCount();
  keyIndex_.reserve(n);
  GridItem item;
  for (size_t i = 0; i < n; ++i) {
    model_->item(modelIndex(i), item);
    keyIndex_.emplace(item.key, static_cast<uint32_t>(i));
  }
  keyIndexVersion_ = shownVersion_;
}

size_t ThumbnailGrid::indexOfKey(uint64_t key) const {
  if (model_ == nullptr || shownCount() == 0) return kNone;
  rebuildKeyIndex();
  const auto it = keyIndex_.find(key);
  return it == keyIndex_.end() ? kNone : static_cast<size_t>(it->second);
}

void ThumbnailGrid::setModel(const AssetModel* model) {
  cancelRename();
  model_ = model;
  selected_.clear();
  hasCursor_ = false;
  cursor_ = anchor_ = 0;
  scroll_ = 0.0;
  if (scheduler_) scheduler_->invalidateAll();
  restartFilter();
  requestPaint();
}

void ThumbnailGrid::modelChanged() {
  cancelRename();
  restartFilter();
  rebuildSelectionAfterModelChange();
  requestPaint();
}

void ThumbnailGrid::setFilter(std::function<bool(const GridItem&)> filter) {
  filter_ = std::move(filter);
  search_.clear();
  restartFilter();
  rebuildSelectionAfterModelChange();
  scroll_ = 0.0;
  requestPaint();
}

void ThumbnailGrid::setSearchText(std::string text) {
  if (text.size() > 1024) text.resize(1024);
  search_ = std::move(text);
  if (search_.empty()) {
    filter_ = nullptr;
  } else {
    const std::string needle = search_;
    filter_ = [needle](const GridItem& item) { return thumbs::findInsensitive(item.name, needle) != std::string_view::npos; };
  }
  restartFilter();
  rebuildSelectionAfterModelChange();
  scroll_ = 0.0;
  requestPaint();
}

void ThumbnailGrid::setSort(SortMode mode) {
  if (mode == sort_) return;
  sort_ = mode;
  restartFilter();
  requestPaint();
}

void ThumbnailGrid::restartFilter() {
  ++shownVersion_;
  shown_.clear();
  filterNext_ = 0;
  if (!filter_ && sort_ == SortMode::Model) {
    identity_ = true;
    filterRunning_ = false;
  } else {
    identity_ = false;
    filterRunning_ = true;
    if (!filter_) {  // sorting only: every item, then sort in one go
      const size_t n = modelCount();
      shown_.resize(n);
      std::iota(shown_.begin(), shown_.end(), 0u);
      filterNext_ = n;
    }
    ui().invalidator().requestAnimation(id());
    animating_ = true;
  }
  clampScroll();
}

void ThumbnailGrid::pumpFilter() {
  if (!filterRunning_) return;
  const size_t n = modelCount();
  const double start = steadyMs();
  GridItem item;
  while (filterNext_ < n) {
    for (int k = 0; k < 64 && filterNext_ < n; ++k, ++filterNext_) {
      if (!filter_) break;
      model_->item(filterNext_, item);
      if (filter_(item)) shown_.push_back(static_cast<uint32_t>(filterNext_));
    }
    if (steadyMs() - start >= kFilterBudgetMs) break;  // the rest runs in the next frame (rule 49)
  }
  ++shownVersion_;
  if (filterNext_ >= n) {
    filterRunning_ = false;
    finishSort();
    ++shownVersion_;
    rebuildSelectionAfterModelChange();
  }
  clampScroll();
}

void ThumbnailGrid::finishSort() {
  if (sort_ == SortMode::Model || model_ == nullptr) return;
  std::vector<std::pair<std::string, uint32_t>> names;
  names.reserve(shown_.size());
  GridItem item;
  for (const uint32_t i : shown_) {
    model_->item(i, item);
    names.emplace_back(std::move(item.name), i);
  }
  const bool descending = sort_ == SortMode::NameDescending;
  std::stable_sort(names.begin(), names.end(), [&](const auto& a, const auto& b) {
    return descending ? thumbs::naturalLess(b.first, a.first) : thumbs::naturalLess(a.first, b.first);
  });
  for (size_t i = 0; i < names.size(); ++i) shown_[i] = names[i].second;
}

void ThumbnailGrid::rebuildSelectionAfterModelChange() {
  if (selected_.empty() && !hasCursor_) return;
  // Keep the keys that are still shown; a lost selection notifies once (spec 08 rules 71, 73).
  std::unordered_set<uint64_t> present;
  const size_t n = shownCount();
  GridItem item;
  if (!selected_.empty() || hasCursor_) {
    present.reserve(std::min<size_t>(n, 1u << 20));
    for (size_t i = 0; i < n && model_ != nullptr; ++i) {
      model_->item(modelIndex(i), item);
      present.insert(item.key);
    }
  }
  const size_t before = selected_.size();
  for (auto it = selected_.begin(); it != selected_.end();) it = present.count(*it) != 0 ? std::next(it) : selected_.erase(it);
  if (hasCursor_ && present.count(cursor_) == 0) hasCursor_ = false;
  if (present.count(anchor_) == 0) anchor_ = hasCursor_ ? cursor_ : 0;
  if (selected_.size() != before && onSelectionChanged) onSelectionChanged();
}

// ---- pictures ---------------------------------------------------------------------------------

void ThumbnailGrid::setProvider(thumbs::ThumbnailProvider* provider) {
  provider_ = provider;
  if (scheduler_) scheduler_->setProvider(provider);
  requestPaint();
}

void ThumbnailGrid::setTextureSink(thumbs::ThumbnailTextureSink* sink) {
  sink_ = sink;
  scheduler_.reset();  // the scheduler points into the cache
  cache_.reset();
  if (sink_ != nullptr) {
    cache_ = std::make_unique<thumbs::ThumbnailCache>(*sink_, limits_);
    scheduler_ = std::make_unique<thumbs::ThumbnailScheduler>(*cache_);
    scheduler_->setProvider(provider_);
  }
  requestPaint();
}

void ThumbnailGrid::setCacheLimits(const thumbs::CacheLimits& limits) {
  limits_ = limits;
  if (cache_) cache_->setLimits(limits);
}

void ThumbnailGrid::invalidateThumbnail(uint64_t key) {
  if (scheduler_) scheduler_->invalidate(key);
  requestPaint();
}

void ThumbnailGrid::notifyThumbnailReady(uint64_t key) {
  if (scheduler_) scheduler_->notifyReady(key);
  requestPaint();
}

size_t ThumbnailGrid::cachedThumbnails() const { return cache_ ? cache_->size() : 0; }
size_t ThumbnailGrid::thumbnailRequests() const { return scheduler_ ? scheduler_->requested() : 0; }
bool ThumbnailGrid::hasThumbnail(uint64_t key) const { return cache_ && cache_->contains(key); }

uint32_t ThumbnailGrid::bucketFor(double scale) const {
  const double edge = (mode_ == thumbs::ViewMode::List ? std::clamp(std::round(zoom_[1] * 0.5), 24.0, 96.0) : zoom_[0]) * std::max(0.1, scale);
  return edge <= 64.0 ? 64u : (edge <= 128.0 ? 128u : 256u);
}

void ThumbnailGrid::pumpThumbnails(PaintContext& ctx, const thumbs::Metrics& m) {
  wanted_.clear();
  protectedKeys_.clear();
  if (!scheduler_ || m.itemCount == 0) {
    if (animating_ && !filterRunning_) {
      ui().invalidator().cancelAnimation(id());
      animating_ = false;
    }
    return;
  }
  const thumbs::VisibleRange vis = thumbs::visibleRange(m, scroll_, viewportHeight(), 0);
  const size_t n = shownCount();
  GridItem item;
  // Visible items first, in reading order, then the margin (kept in the cache and prefetched).
  for (size_t i = vis.first; i < vis.last; ++i) {
    fetch(i, item);
    wanted_.push_back(item.key);
    protectedKeys_.insert(item.key);
  }
  const size_t lo = vis.first > kPrefetchItems ? vis.first - kPrefetchItems : 0;
  const size_t hi = std::min(n, vis.last + kPrefetchItems);
  for (size_t i = vis.last; i < hi; ++i) {
    fetch(i, item);
    wanted_.push_back(item.key);
    protectedKeys_.insert(item.key);
  }
  for (size_t i = vis.first; i-- > lo;) {
    fetch(i, item);
    wanted_.push_back(item.key);
    protectedKeys_.insert(item.key);
  }
  const bool remaining = scheduler_->pump(wanted_, bucketFor(ctx.scale()), [this](uint64_t k) { return protectedKeys_.count(k) != 0; }, steadyMs);
  if (remaining || filterRunning_) {
    if (!animating_) {
      ui().invalidator().requestAnimation(id());
      animating_ = true;
    }
  } else if (animating_) {
    ui().invalidator().cancelAnimation(id());
    animating_ = false;
  }
}

// ---- view -------------------------------------------------------------------------------------

thumbs::Metrics ThumbnailGrid::computeMetrics() const {
  thumbs::LayoutParams p;
  const core::layout::Rect r = ui().absRect(id());
  p.viewportWidth = r.w;
  p.viewportHeight = r.h;
  p.mode = mode_;
  p.thumbEdge = zoom_[static_cast<size_t>(mode_)];
  p.itemCount = shownCount();
  return thumbs::computeMetrics(p);
}

thumbs::Metrics ThumbnailGrid::metrics() const { return computeMetrics(); }

double ThumbnailGrid::viewportHeight() const { return std::max(0.0, static_cast<double>(ui().absRect(id()).h)); }

thumbs::VisibleRange ThumbnailGrid::visibleRange() const { return thumbs::visibleRange(computeMetrics(), scroll_, viewportHeight(), 0); }

thumbs::Rect ThumbnailGrid::itemViewRect(size_t shownIndex) const {
  thumbs::Rect r = thumbs::itemRect(computeMetrics(), shownIndex);
  r.y -= scroll_;
  return r;
}

void ThumbnailGrid::clampScroll() { scroll_ = thumbs::clampScroll(computeMetrics(), scroll_, viewportHeight()); }

void ThumbnailGrid::setScroll(double value) {
  const double next = thumbs::clampScroll(computeMetrics(), value, viewportHeight());
  if (next == scroll_) return;
  scroll_ = next;
  requestPaint();
}

void ThumbnailGrid::setScrollOffset(double offset) { setScroll(offset); }

void ThumbnailGrid::scrollToKey(uint64_t key, bool center) {
  const size_t i = indexOfKey(key);
  if (i == kNone) return;
  setScroll(thumbs::revealScroll(computeMetrics(), scroll_, viewportHeight(), i, center));
}

void ThumbnailGrid::setViewMode(thumbs::ViewMode mode) {
  if (mode == mode_) return;
  cancelRename();
  // Keep the first visible item in view across the switch (the selection is kept by key).
  const size_t anchorIndex = thumbs::visibleRange(computeMetrics(), scroll_, viewportHeight(), 0).first;
  mode_ = mode;
  const thumbs::Metrics m = computeMetrics();
  scroll_ = thumbs::clampScroll(m, thumbs::itemRect(m, anchorIndex).y - m.padding, viewportHeight());
  requestPaint();
}

void ThumbnailGrid::setZoom(thumbs::ViewMode mode, double value) {
  const double next = thumbs::clampZoom(value);
  double& slot = zoom_[static_cast<size_t>(mode)];
  if (next == slot) return;
  if (mode == mode_) cancelRename();
  const size_t first = mode == mode_ ? thumbs::visibleRange(computeMetrics(), scroll_, viewportHeight(), 0).first : kNone;
  slot = next;
  if (mode == mode_) {
    const thumbs::Metrics m = computeMetrics();
    if (first != kNone) scroll_ = thumbs::clampScroll(m, thumbs::itemRect(m, first).y - m.padding, viewportHeight());
  }
  requestPaint();
  if (onZoomChanged) onZoomChanged(mode, next);
}

void ThumbnailGrid::stepZoom(int direction) { setZoom(mode_, thumbs::stepZoom(zoom_[static_cast<size_t>(mode_)], direction)); }

// ---- selection storage ------------------------------------------------------------------------

std::vector<uint64_t> ThumbnailGrid::selectedKeys() const {
  // In shown order, so drag payloads and callbacks are deterministic.
  std::vector<std::pair<size_t, uint64_t>> ordered;
  if (selected_.empty()) return {};
  rebuildKeyIndex();
  for (const uint64_t key : selected_) {
    const auto it = keyIndex_.find(key);
    ordered.emplace_back(it == keyIndex_.end() ? thumbs::kMaxItems : static_cast<size_t>(it->second), key);
  }
  std::sort(ordered.begin(), ordered.end());
  std::vector<uint64_t> keys;
  keys.reserve(ordered.size());
  for (const auto& p : ordered) keys.push_back(p.second);
  return keys;
}

void ThumbnailGrid::commitSelection(std::unordered_set<uint64_t> next) {
  if (next == selected_) return;
  selected_ = std::move(next);
  requestPaint();
  if (onSelectionChanged) onSelectionChanged();
}

void ThumbnailGrid::setSelection(const std::vector<uint64_t>& keys) {
  std::unordered_set<uint64_t> next(keys.begin(), keys.end());
  if (next == selected_) return;
  selected_ = std::move(next);
  hasCursor_ = !keys.empty();
  cursor_ = anchor_ = keys.empty() ? 0 : keys.back();
  requestPaint();
}

void ThumbnailGrid::clearSelection() { commitSelection({}); }

void ThumbnailGrid::selectAll() {
  std::unordered_set<uint64_t> all;
  const size_t n = shownCount();
  all.reserve(std::min<size_t>(n, 1u << 20));
  GridItem item;
  for (size_t i = 0; i < n; ++i) {
    fetch(i, item);
    all.insert(item.key);
  }
  commitSelection(std::move(all));
}

}  // namespace r1ui::widgets
