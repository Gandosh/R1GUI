// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CreatorSession, the state of one menu-creator window that must outlive the widget: the draft being
//   edited (MenuDraft), whether the user has already chosen the type, and the listeners that tell the
//   window to rebuild when the host replaces the draft (it starts an edit, loads a file).
// Why: the dock recreates a panel's content when the panel moves into another native window, and the host
//   opens the creator from several places (Create, Edit entries of the Custom Menus menu, a file load). A
//   widget that owned the draft would lose the user's work in the first case and could not be steered in
//   the second; the session is the small object both sides share.
// Callers: the host (begin* before it shows the window, end after Create/Cancel), the creator window
//   (reads and edits the draft), tests. Calls: MenuDraft, CustomMenuSet.
// Lifetime: the session and the live set must outlive every window that uses them; the window
//   unsubscribes in onDetached. UI thread only.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "r1ui/commands/custommenu/MenuDraft.h"

namespace r1ui::widgets {

class CreatorSession {
 public:
  using Listener = std::function<void()>;
  using ListenerId = uint32_t;

  explicit CreatorSession(commands::custommenu::CustomMenuSet& live) : live_(live) {}

  commands::custommenu::CustomMenuSet& live() { return live_; }
  const commands::custommenu::CustomMenuSet& live() const { return live_; }

  // The draft being edited; nullptr between end() and the next begin.
  commands::custommenu::MenuDraft* draft() const { return draft_.get(); }
  // The draft, started as a new pie menu with a free name when there is none.
  commands::custommenu::MenuDraft& ensure();
  bool active() const { return draft_ != nullptr; }
  // True when the user (or the host) has chosen the type, so the window shows the editor and not the type
  // chooser. Always true for an edit and for a draft read from a file.
  bool typeChosen() const { return typeChosen_; }
  // Counts every replacement of the draft and every type choice; a window rebuilds when it changes.
  uint64_t generation() const { return generation_; }

  // A new menu: the window shows the type chooser (kind unset) or goes straight to the editor.
  void beginCreate(std::optional<commands::custommenu::MenuKind> kind = std::nullopt);
  // Edits the menu with that id; false (nothing changed) when it does not exist.
  bool beginEdit(const std::string& menuId);
  // A new menu that starts from a menu read from a file; false when the model refuses it.
  bool beginFromFile(const commands::custommenu::CustomMenu& menu);
  // The user picked the type in the chooser.
  void chooseType(commands::custommenu::MenuKind kind);
  // Drops the draft (Cancel, or after a successful Create/Save).
  void end();

  ListenerId subscribe(Listener listener);
  void unsubscribe(ListenerId id);

 private:
  void notify();

  commands::custommenu::CustomMenuSet& live_;
  std::unique_ptr<commands::custommenu::MenuDraft> draft_;
  bool typeChosen_ = false;
  uint64_t generation_ = 1;
  std::vector<std::pair<ListenerId, Listener>> listeners_;
  ListenerId next_ = 1;
};

}  // namespace r1ui::widgets
