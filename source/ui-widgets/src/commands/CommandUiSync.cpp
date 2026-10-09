// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CommandUiSync.h: the refresher hub and the registry subscription.
// Invariants: refreshers run in attachment order; a refresher may attach or detach others (the list
//   is copied before the run); refresh() never nests.
// Callers: hosts, bindCommandToolbar, KeybindingEditor, tests.
#include "r1ui/widgets/commands/CommandUiSync.h"

#include <algorithm>

#include "r1ui/widgets/commands/CommandMenus.h"

namespace r1ui::widgets {

class CommandUiSync::Hub {
 public:
  Token attach(Refresher refresher) {
    const Token token = next_++;
    list_.emplace_back(token, std::move(refresher));
    return token;
  }
  void detach(Token token) {
    list_.erase(std::remove_if(list_.begin(), list_.end(), [&](const auto& entry) { return entry.first == token; }), list_.end());
  }
  void run() {
    if (running_) return;
    running_ = true;
    ++count_;
    const auto snapshot = list_;
    for (const auto& [token, refresher] : snapshot) {
      const bool stillAttached = std::any_of(list_.begin(), list_.end(), [&](const auto& entry) { return entry.first == token; });
      if (stillAttached) refresher();
    }
    running_ = false;
  }
  uint64_t count() const { return count_; }

 private:
  std::vector<std::pair<Token, Refresher>> list_;
  Token next_ = 1;
  bool running_ = false;
  uint64_t count_ = 0;
};

CommandUiSync::Attachment& CommandUiSync::Attachment::operator=(Attachment&& other) noexcept {
  if (this != &other) {
    reset();
    hub_ = std::move(other.hub_);
    token_ = std::exchange(other.token_, 0);
  }
  return *this;
}

void CommandUiSync::Attachment::reset() {
  if (token_ == 0) return;
  if (const std::shared_ptr<Hub> hub = hub_.lock()) hub->detach(token_);
  token_ = 0;
  hub_.reset();
}

CommandUiSync::CommandUiSync(UiContext& ui, CommandServices services) : ui_(ui), services_(services), hub_(std::make_shared<Hub>()) {
  listener_ = services_.registry.subscribe([this] { refresh(); });
}

CommandUiSync::~CommandUiSync() { services_.registry.unsubscribe(listener_); }

CommandUiSync::Attachment CommandUiSync::attach(Refresher refresher) {
  if (!refresher) return {};
  return Attachment(hub_, hub_->attach(std::move(refresher)));
}

void CommandUiSync::refresh() {
  refreshOpenCommandMenus(ui_, services_);
  hub_->run();
}

uint64_t CommandUiSync::refreshCount() const { return hub_->count(); }

}  // namespace r1ui::widgets
