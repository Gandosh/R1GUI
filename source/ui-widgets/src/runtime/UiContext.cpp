// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: UiContext construction, widget creation / destruction / lookup, viewport and window state,
//   measurement dispatch and the frame function (layout, overlay placement, layout callbacks).
//   Input translation lives in UiContextInput.cpp, painting and animation in UiContextPaint.cpp.
// Invariants: objects_[slot] is the object of every widget whose userData == slot + 1; a destroyed
//   widget's object moves to graveyard_ and is freed only when no event dispatch is on the stack;
//   the overlay layer is the last child of the root and is never destroyed before the context.
// Callers: the shell, widgets, tests.
#include "r1ui/widgets/runtime/UiContext.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "r1ui/core/CheckedCast.h"

namespace r1ui::widgets {

namespace tree = core::tree;
using tree::WidgetId;

namespace {

WidgetId makeRoot(tree::WidgetTree& t) {
  const tree::CreateResult r = t.createRoot();
  if (!r.ok()) throw std::runtime_error(std::string("cannot create the root widget: ") + tree::describe(r.error));
  return r.id;
}

constexpr int kMaxFramePasses = 5;

}  // namespace

// ---- construction -----------------------------------------------------------------------------

UiContext::UiContext(Services& services, UiContextOptions options)
    : services_(services),
      host_(std::move(options.host)),
      tree_(options.limits),
      router_(tree_, makeRoot(tree_), options.router),
      invalidator_(tree_),
      overlays_(*this),
      tooltips_(*this) {
  root_ = router_.root();
  invalidator_.setRoot(root_, 0.0, 0.0);
  router_.setGlobalKeyHandler(this);
  overlays_.createLayer();
  seenThemeRevision_ = services_.theme().revision();
  seenSheetRevision_ = services_.sheetRevision();
}

UiContext::~UiContext() {
  // Handlers are non-owning pointers inside the nodes: clear them before the objects go so nothing
  // can dispatch into a half-destroyed widget.
  tree_.forEachDescendant(
      root_, [&](WidgetId id) {
        if (tree::Widget* w = tree_.get(id)) w->handler = nullptr;
      },
      true);
}

core::layout::Style& UiContext::rootStyle() {
  invalidator_.requestLayout(root_);
  return tree_.get(root_)->style;
}

// ---- widgets ----------------------------------------------------------------------------------

void UiContext::attach(WidgetId parent, WidgetId before, std::unique_ptr<WidgetObject> object) {
  if (!tree_.alive(parent)) throw std::invalid_argument("UiContext::create: the parent widget does not exist");
  const tree::CreateResult created = invalidator_.create(parent, before);
  if (!created.ok()) throw std::length_error(std::string("UiContext::create: ") + tree::describe(created.error));
  uint32_t slot = 0;
  if (!freeSlots_.empty()) {
    slot = freeSlots_.back();
    freeSlots_.pop_back();
    objects_[slot] = std::move(object);
  } else {
    slot = core::checkedCast<uint32_t>(objects_.size());
    objects_.push_back(std::move(object));
  }
  WidgetObject* raw = objects_[slot].get();
  tree::Widget* node = tree_.get(created.id);
  node->userData = uint64_t{slot} + 1;
  node->handler = raw;
  node->name = raw->typeName();
  raw->bind(*this, created.id);
  try {
    raw->onAttached();
  } catch (...) {
    destroy(created.id);
    throw;
  }
}

WidgetObject* UiContext::object(WidgetId id) {
  tree::Widget* w = tree_.get(id);
  if (w == nullptr || w->userData == 0 || w->userData > objects_.size()) return nullptr;
  return objects_[static_cast<size_t>(w->userData - 1)].get();
}

const WidgetObject* UiContext::object(WidgetId id) const {
  const tree::Widget* w = tree_.get(id);
  if (w == nullptr || w->userData == 0 || w->userData > objects_.size()) return nullptr;
  return objects_[static_cast<size_t>(w->userData - 1)].get();
}

core::layout::Rect UiContext::absRect(WidgetId id) const {
  const tree::Widget* w = tree_.get(id);
  return w != nullptr ? w->absRect : core::layout::Rect{};
}

bool UiContext::reparent(WidgetId child, WidgetId newParent, WidgetId before) {
  return invalidator_.reparent(child, newParent, before) == tree::TreeError::None;
}

bool UiContext::destroy(WidgetId id) {
  if (!tree_.alive(id) || id == root_) return false;
  std::vector<WidgetId> subtree;
  tree_.forEachDescendant(id, [&](WidgetId d) { subtree.push_back(d); }, true);
  // Slots are read now: once the tree destroys the nodes the ids no longer resolve.
  std::vector<size_t> slots;
  slots.reserve(subtree.size());
  for (const WidgetId d : subtree) {
    const tree::Widget* w = tree_.get(d);
    if (w != nullptr && w->userData != 0 && w->userData <= objects_.size()) slots.push_back(static_cast<size_t>(w->userData - 1));
  }
  // Children first, so a parent's onDetached still sees its children gone.
  for (size_t i = subtree.size(); i-- > 0;) {
    if (WidgetObject* o = object(subtree[i])) o->onDetached();
  }
  const tree::TreeError error = invalidator_.destroy(id);
  if (error != tree::TreeError::None) return false;  // Busy: destroyed during layout, nothing changed
  for (const size_t slot : slots) {
    graveyard_.push_back(std::move(objects_[slot]));
    freeSlots_.push_back(core::checkedCast<uint32_t>(slot));
  }
  if (dispatchDepth_ == 0) {
    router_.sync();
    graveyard_.clear();
  }
  return true;
}

void UiContext::setLayoutCallback(WidgetId id, bool wants) {
  const auto it = std::find(layoutCallbacks_.begin(), layoutCallbacks_.end(), id);
  if (wants && it == layoutCallbacks_.end()) layoutCallbacks_.push_back(id);
  if (!wants && it != layoutCallbacks_.end()) layoutCallbacks_.erase(it);
}

core::layout::MeasureResult UiContext::measure(WidgetId widget, const core::layout::MeasureInput& input) {
  WidgetObject* o = object(widget);
  return o != nullptr ? o->measure(input) : core::layout::MeasureResult{};
}

// ---- viewport and window ------------------------------------------------------------------------

void UiContext::setViewport(int physicalWidth, int physicalHeight, float scale) {
  const float safeScale = std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
  const int w = std::max(0, physicalWidth);
  const int h = std::max(0, physicalHeight);
  if (w == viewportW_ && h == viewportH_ && safeScale == scale_) return;
  const bool scaleChanged = safeScale != scale_;
  viewportW_ = w;
  viewportH_ = h;
  scale_ = safeScale;
  invalidator_.setRoot(root_, viewportWidth(), viewportHeight());
  if (scaleChanged) invalidator_.requestFullLayout();
  invalidator_.requestPaint(root_);
}

double UiContext::viewportWidth() const { return static_cast<double>(viewportW_) / static_cast<double>(scale_); }
double UiContext::viewportHeight() const { return static_cast<double>(viewportH_) / static_cast<double>(scale_); }

void UiContext::setWindowActive(bool active) {
  if (active == windowActive_) return;
  windowActive_ = active;
  if (!active) {
    DispatchGuard guard(*this);
    overlays_.windowDeactivated();
    tooltips_.hide();
    router_.cancelPointerInteraction();
  }
  invalidator_.requestPaint(root_);
}

// ---- frame --------------------------------------------------------------------------------------

bool UiContext::needsFrame() const {
  return invalidator_.needsFrame() || overlays_.needsPlacement() || repaintRequested_ || seenThemeRevision_ != services_.theme().revision() ||
         seenSheetRevision_ != services_.sheetRevision();
}

FrameInfo UiContext::frame() {
  DispatchGuard guard(*this);
  FrameInfo info;
  if (seenSheetRevision_ != services_.sheetRevision()) {
    seenSheetRevision_ = services_.sheetRevision();
    invalidator_.requestFullLayout();  // new rows can change font sizes and paddings
  }
  if (seenThemeRevision_ != services_.theme().revision()) {
    seenThemeRevision_ = services_.theme().revision();
    invalidator_.requestPaint(root_);
  }
  repaintRequested_ = false;
  for (int pass = 0; pass < kMaxFramePasses; ++pass) {
    core::invalidation::FrameResult result = invalidator_.runFrame(this);
    info.layoutRan = info.layoutRan || result.layoutRan;
    info.damage.insert(info.damage.end(), result.damage.begin(), result.damage.end());
    if (result.layoutRan) {
      router_.sync();
      const std::vector<WidgetId> callbacks = layoutCallbacks_;
      for (const WidgetId id : callbacks) {
        if (WidgetObject* o = object(id)) o->onLayout();
      }
    }
    const bool placed = overlays_.afterLayout();
    const bool moved = tooltips_.afterLayout();
    // A layout callback may have changed the layout again (a scroll area adding its scrollbar gutter).
    if (!placed && !moved && !invalidator_.layoutPending()) break;
  }
  overlays_.enforceFocusTrap();
  return info;
}

bool UiContext::tick() {
  DispatchGuard guard(*this);
  const bool tooltip = tooltips_.tick();
  const bool timer = runDueTimers();
  return tooltip || timer;
}

std::optional<uint64_t> UiContext::msUntilTick() const {
  const std::optional<uint64_t> tooltip = tooltips_.msUntilTick();
  const std::optional<uint64_t> timer = msUntilTimer();
  if (tooltip && timer) return std::min(*tooltip, *timer);
  return tooltip ? tooltip : timer;
}

bool UiContext::consumeRepaint() {
  const bool was = repaintRequested_;
  repaintRequested_ = false;
  return was;
}

}  // namespace r1ui::widgets
