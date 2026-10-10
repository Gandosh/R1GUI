// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CreatorSession.h.
// Invariants: the draft is replaced only through the begin* calls and end(), each of which bumps the
//   generation and notifies once; listeners are called with no iterator held (a listener may unsubscribe
//   itself or start another draft).
// Callers: the host, the creator window, tests.
#include "r1ui/widgets/custommenu/creator/CreatorSession.h"

#include <algorithm>

namespace r1ui::widgets {

namespace cm = commands::custommenu;

cm::MenuDraft& CreatorSession::ensure() {
  if (!draft_) {
    draft_ = cm::MenuDraft::create(cm::MenuKind::Pie, live_.uniqueName("New menu"));
    typeChosen_ = false;
    ++generation_;
  }
  return *draft_;
}

void CreatorSession::beginCreate(std::optional<cm::MenuKind> kind) {
  draft_ = cm::MenuDraft::create(kind.value_or(cm::MenuKind::Pie), live_.uniqueName(kind && *kind == cm::MenuKind::Panel ? "New panel" : "New menu"));
  typeChosen_ = kind.has_value();
  ++generation_;
  notify();
}

bool CreatorSession::beginEdit(const std::string& menuId) {
  std::unique_ptr<cm::MenuDraft> next = cm::MenuDraft::edit(live_, menuId);
  if (!next) return false;
  draft_ = std::move(next);
  typeChosen_ = true;
  ++generation_;
  notify();
  return true;
}

bool CreatorSession::beginFromFile(const cm::CustomMenu& menu) {
  std::unique_ptr<cm::MenuDraft> next = cm::MenuDraft::fromContent(menu);
  if (!next) return false;
  draft_ = std::move(next);
  typeChosen_ = true;
  ++generation_;
  notify();
  return true;
}

void CreatorSession::chooseType(cm::MenuKind kind) {
  ensure();
  draft_->setKind(kind);
  if (kind == cm::MenuKind::Panel && draft_->name() == live_.uniqueName("New menu")) draft_->setName(live_.uniqueName("New panel"));
  typeChosen_ = true;
  ++generation_;
  notify();
}

void CreatorSession::end() {
  if (!draft_ && !typeChosen_) return;
  draft_.reset();
  typeChosen_ = false;
  ++generation_;
  notify();
}

CreatorSession::ListenerId CreatorSession::subscribe(Listener listener) {
  const ListenerId id = next_++;
  listeners_.emplace_back(id, std::move(listener));
  return id;
}

void CreatorSession::unsubscribe(ListenerId id) {
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [&](const auto& entry) { return entry.first == id; }), listeners_.end());
}

void CreatorSession::notify() {
  std::vector<ListenerId> ids;
  ids.reserve(listeners_.size());
  for (const auto& entry : listeners_) ids.push_back(entry.first);
  for (const ListenerId id : ids) {
    Listener listener;
    for (const auto& entry : listeners_) {
      if (entry.first == id) listener = entry.second;
    }
    if (listener) listener();
  }
}

}  // namespace r1ui::widgets
