// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Toast.h: the ToastStack shared state (entries, countdown timers, pause on
//   hover, fade-out and removal) and the ToastManager handle.
// Invariants: every entry has at most one live timer (countdown or fade-out); an entry is removed
//   exactly once and its onClosed callback runs once, after its widget is gone; a paused toast has
//   no countdown timer and keeps its remaining time.
// Callers: application code, tests, the gallery.
#include "r1ui/widgets/toast/Toast.h"

#include <algorithm>
#include <stdexcept>

#include "r1ui/widgets/menu/MenuModel.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/toast/ToastParts.h"

namespace r1ui::widgets {

using core::tree::WidgetId;

namespace {
constexpr uint64_t kMinResumeMs = 500;  // a toast the pointer just left stays at least this long
}  // namespace

class ToastStack final : public std::enable_shared_from_this<ToastStack> {
 public:
  explicit ToastStack(UiContext& ui) : ui_(&ui) {}

  ToastId show(ToastSpec spec);
  bool dismiss(ToastId id);
  void dismissAll();
  size_t count() const { return entries_.size(); }
  WidgetId widgetOf(ToastId id) const {
    const Entry* e = find(id);
    return e != nullptr ? e->widget : WidgetId{};
  }
  ToastTiming timing_;
  size_t maxVisible_ = 5;

 private:
  struct Entry {
    ToastId id = 0;
    WidgetId widget;
    UiContext::TimerId timer = 0;
    uint64_t dueMs = 0;
    uint64_t remainingMs = 0;
    bool paused = false;
    bool closing = false;
    std::function<void()> onClosed;
  };

  Entry* find(ToastId id) {
    for (Entry& e : entries_) {
      if (e.id == id) return &e;
    }
    return nullptr;
  }
  const Entry* find(ToastId id) const { return const_cast<ToastStack*>(this)->find(id); }
  void ensureLayer();
  void startCountdown(Entry& e, uint64_t ms);
  void setHover(ToastId id, bool hovered);
  void copyText(ToastId id);
  void removeNow(ToastId id);
  uint64_t durationFor(const ToastSpec& spec) const;

  UiContext* ui_;
  WidgetId layer_;
  std::vector<Entry> entries_;
  ToastId nextId_ = 1;
};

uint64_t ToastStack::durationFor(const ToastSpec& spec) const {
  if (spec.durationMs != 0) return spec.durationMs;
  switch (spec.tone) {
    case ToastTone::Warning: return timing_.warningMs;
    case ToastTone::Error: return timing_.errorMs;
    case ToastTone::Default: break;
  }
  return timing_.defaultMs;
}

void ToastStack::ensureLayer() {
  if (ui_->alive(layer_)) return;
  layer_ = ui_->create<ToastLayer>(ui_->overlays().layer()).id();
}

ToastId ToastStack::show(ToastSpec spec) {
  spec.text = sanitizeUtf8(spec.text, kMaxToastTextBytes);
  spec.icon = sanitizeUtf8(spec.icon, 64);
  if (spec.text.empty()) return 0;
  const std::shared_ptr<ToastStack> self = shared_from_this();
  // Make room first: the oldest toast goes at once (no fade) when the limit would be exceeded.
  while (!entries_.empty() && entries_.size() >= std::max<size_t>(1, maxVisible_)) removeNow(entries_.front().id);
  try {
    ensureLayer();
    const ToastId id = nextId_++;
    ToastWidget::Callbacks callbacks;
    callbacks.onHover = [weak = weak_from_this(), id](bool hovered) {
      if (const std::shared_ptr<ToastStack> s = weak.lock()) s->setHover(id, hovered);
    };
    callbacks.onCopy = [weak = weak_from_this(), id]() {
      if (const std::shared_ptr<ToastStack> s = weak.lock()) s->copyText(id);
    };
    callbacks.onClose = [weak = weak_from_this(), id]() {
      if (const std::shared_ptr<ToastStack> s = weak.lock()) s->dismiss(id);
    };
    ToastWidget& widget = ui_->create<ToastWidget>(layer_, spec.text, spec.tone, spec.icon, spec.controls, timing_.fadeMs, std::move(callbacks));
    Entry entry;
    entry.id = id;
    entry.widget = widget.id();
    entry.onClosed = std::move(spec.onClosed);
    const uint64_t duration = durationFor(spec);
    entries_.push_back(std::move(entry));
    if (duration != kToastNeverExpires) startCountdown(entries_.back(), duration);
    return id;
  } catch (const std::exception&) {
    return 0;  // the tree is full or the layer could not be created: nothing was shown
  }
}

void ToastStack::startCountdown(Entry& e, uint64_t ms) {
  if (e.timer != 0) ui_->cancelTimer(e.timer);
  e.dueMs = ui_->now() + ms;
  e.remainingMs = ms;
  e.paused = false;
  const ToastId id = e.id;
  e.timer = ui_->setTimer(ms, [weak = weak_from_this(), id]() {
    if (const std::shared_ptr<ToastStack> s = weak.lock()) {
      if (Entry* entry = s->find(id)) entry->timer = 0;
      s->dismiss(id);
    }
  });
}

void ToastStack::setHover(ToastId id, bool hovered) {
  Entry* e = find(id);
  if (e == nullptr || e->closing) return;
  if (hovered) {
    if (e->paused || e->timer == 0) return;  // never expires, or already paused
    ui_->cancelTimer(e->timer);
    e->timer = 0;
    e->remainingMs = e->dueMs > ui_->now() ? e->dueMs - ui_->now() : 0;
    e->paused = true;
  } else if (e->paused) {
    startCountdown(*e, std::max(e->remainingMs, kMinResumeMs));
  }
}

void ToastStack::copyText(ToastId id) {
  const Entry* e = find(id);
  if (e == nullptr) return;
  const ToastWidget* widget = ui_->objectAs<ToastWidget>(e->widget);
  if (widget != nullptr && ui_->host().writeClipboard) ui_->host().writeClipboard(widget->text());
}

bool ToastStack::dismiss(ToastId id) {
  Entry* e = find(id);
  if (e == nullptr) return false;
  if (e->closing) return true;
  const std::shared_ptr<ToastStack> self = shared_from_this();
  e->closing = true;
  if (e->timer != 0) ui_->cancelTimer(e->timer);
  e->timer = 0;
  if (!ui_->animationsActive() || timing_.fadeMs == 0) {
    removeNow(id);
    return true;
  }
  if (ToastWidget* widget = ui_->objectAs<ToastWidget>(e->widget)) widget->beginClose();
  e->timer = ui_->setTimer(timing_.fadeMs, [weak = weak_from_this(), id]() {
    if (const std::shared_ptr<ToastStack> s = weak.lock()) {
      if (Entry* entry = s->find(id)) entry->timer = 0;
      s->removeNow(id);
    }
  });
  return true;
}

void ToastStack::removeNow(ToastId id) {
  Entry* e = find(id);
  if (e == nullptr) return;
  const std::shared_ptr<ToastStack> self = shared_from_this();
  if (e->timer != 0) ui_->cancelTimer(e->timer);
  const WidgetId widget = e->widget;
  const std::function<void()> callback = std::move(e->onClosed);
  entries_.erase(entries_.begin() + (e - entries_.data()));
  ui_->destroy(widget);
  if (callback) callback();
}

void ToastStack::dismissAll() {
  std::vector<ToastId> ids;
  for (const Entry& e : entries_) ids.push_back(e.id);
  for (const ToastId id : ids) dismiss(id);
}

// ---- ToastManager handle ------------------------------------------------------------------------

ToastManager::ToastManager(UiContext& ui) : stack_(std::make_shared<ToastStack>(ui)) {}
ToastManager::~ToastManager() = default;

ToastId ToastManager::show(ToastSpec spec) { return stack_->show(std::move(spec)); }
bool ToastManager::dismiss(ToastId id) { return stack_->dismiss(id); }
void ToastManager::dismissAll() { stack_->dismissAll(); }
size_t ToastManager::count() const { return stack_->count(); }
WidgetId ToastManager::widgetOf(ToastId id) const { return stack_->widgetOf(id); }
ToastTiming ToastManager::timing() const { return stack_->timing_; }
void ToastManager::setTiming(const ToastTiming& timing) { stack_->timing_ = timing; }
void ToastManager::setMaxVisible(size_t count) { stack_->maxVisible_ = std::max<size_t>(1, count); }

}  // namespace r1ui::widgets
