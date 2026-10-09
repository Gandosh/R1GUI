// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ChangeNotifier, the small observer list through which a PropertyContext tells its views that the
//   selection, values, row structure or bindings changed.
// Why: spec 09 rules 5-7: a panel must show a value changed elsewhere (a gizmo, a script, undo) within one
//   display refresh, and several changes must cost one rebuild. The notifier delivers synchronously and
//   cheaply (listeners only mark themselves dirty); coalescing to one refresh per frame is the view's job
//   (PropertyPanel does it with a zero-delay timer), so the headless model stays free of any frame clock.
// Callers: PropertyContext (owns one), views and hosts (subscribe), tests.
// Reentrancy: listeners may subscribe or unsubscribe from inside a callback (delivery iterates a
//   snapshot, and a listener removed during delivery is not called afterwards); a listener must not
//   start edits on the same context from inside the callback.
#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace r1ui::props {

enum class ChangeKind : uint8_t {
  Selection,  // the set of selected objects changed: rows were rebuilt
  Values,     // one or more values changed (row = -1: possibly all)
  Structure,  // visibility, enablement or the row list may have changed
  Bindings    // a binding was added, removed or its state may have changed
};

struct ChangeEvent {
  ChangeKind kind = ChangeKind::Values;
  int64_t row = -1;  // the row index when a single row changed, else -1
};

class ChangeNotifier {
 public:
  using Callback = std::function<void(const ChangeEvent&)>;
  using Token = uint64_t;

  Token subscribe(Callback callback) {
    const Token token = ++last_;
    listeners_.push_back({token, std::move(callback)});
    return token;
  }
  void unsubscribe(Token token) {
    for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
      if (it->first == token) {
        listeners_.erase(it);
        return;
      }
    }
  }
  size_t listenerCount() const { return listeners_.size(); }

  void notify(const ChangeEvent& event) {
    const auto snapshot = listeners_;
    for (const auto& [token, callback] : snapshot) {
      if (!callback) continue;
      bool stillSubscribed = false;
      for (const auto& live : listeners_) {
        if (live.first == token) {
          stillSubscribed = true;
          break;
        }
      }
      if (stillSubscribed) callback(event);
    }
  }

 private:
  std::vector<std::pair<Token, Callback>> listeners_;
  Token last_ = 0;
};

}  // namespace r1ui::props
