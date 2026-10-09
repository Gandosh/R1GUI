// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Customization, the live customization state of one window family: the built-in LayoutSet the
//   host supplies, the user's Delta (edited and saved) and a read-only workspace Delta layered on it
//   (the workspace wins), the cached effective layouts, every editing operation with its validation,
//   the edit session (revert to the state at the start), the version counter and listeners that make
//   the UI rebuild, and the hooks the persistence layer uses.
// Why: spec 06 and decisions D1 to D6: users hide, rename, move and add entries, create menus and
//   toolbars, size toolbar buttons and place free-form buttons; every change is a legal difference
//   over the built-in layouts, applies live, and can be undone as a whole for the edit session.
// Callers: the widget layer (edit mode, palette, binders), the host (load/save through
//   CustomizationIo.h), tests. Calls: Delta.h (effectiveLayout), Text.h.
// Editing contract: operations never throw. Each returns an EditResult; on failure nothing changed
//   and `reason` says why in a sentence fit for a tooltip. An operation is validated by applying the
//   candidate delta with effectiveLayout and rejecting it when the report gains a blocking problem
//   (locked node, illegal parent, cycle, missing parent, limit), so the editing rules are exactly the
//   rules the loader applies.
// Views: effective() is the normal display (hidden entries and missing commands dropped); editView()
//   keeps hidden entries (visible = false) and missing commands (missing = true) for the edit mode.
//   Both are cached per version(); references stay valid until the next change.
// Threading: UI thread only. Listeners run synchronously after a change, in subscription order, and
//   must not edit the Customization.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "r1ui/commands/customize/Delta.h"

namespace r1ui::commands::customize {

enum class EditError : uint8_t {
  None,
  UnknownNode,
  UnknownParent,
  Locked,
  Illegal,
  Cycle,
  NotUserNode,
  InvalidText,
  Duplicate,
  OutOfRange,
  UnknownCommand,
  LimitReached
};

struct EditResult {
  bool ok = false;
  EditError error = EditError::None;
  std::string reason;  // empty when ok
  std::string id;      // the node or container created or changed
  Rect rect;           // free-form operations: the rectangle in force afterwards
  explicit operator bool() const { return ok; }
};

// An entry the user hid, for the restore list.
struct RestoreItem {
  std::string id;
  std::string label;  // shown text
  std::string path;   // "Edit > Undo"
  Kind kind = Kind::Command;
};

class Customization {
 public:
  using Listener = std::function<void()>;
  using ListenerId = uint32_t;

  explicit Customization(LayoutSet builtin, CommandExists exists = {});

  // ---- inputs ---------------------------------------------------------------------------------
  const LayoutSet& builtin() const { return builtin_; }
  // A product update: a new built-in set; the user's delta keeps applying by stable ids.
  void setBuiltin(LayoutSet builtin);
  // The registry changed (a command appeared or disappeared): re-evaluate missing references.
  void setCommandExists(CommandExists exists);
  void commandsChanged() { changed(); }

  const Delta& userDelta() const { return user_; }
  const Delta& workspaceDelta() const { return workspace_; }
  // Replace a layer (used by loading, import and tests). Not validated here: effectiveLayout is total.
  void setUserDelta(Delta delta);
  void setWorkspaceDelta(Delta delta);

  // ---- views ----------------------------------------------------------------------------------
  uint64_t version() const { return version_; }
  ListenerId subscribe(Listener listener);
  void unsubscribe(ListenerId id);
  const EffectiveResult& effective() const;
  const EffectiveResult& editView() const;
  // The node with this id in the edit view (hidden entries included), or nullptr.
  const Node* find(const std::string& id) const;
  // "Edit > Undo": the shown labels from the menu down to the node (commands use `commandLabel`).
  std::string pathOf(const std::string& id) const;
  // Text for command entries without a user label; the host wires the registry. Default: the command id.
  void setCommandLabeler(std::function<std::string(const std::string&)> labeler) { labeler_ = std::move(labeler); }
  std::string shownLabel(const Node& node) const;
  std::vector<RestoreItem> restoreList() const;
  bool isLocked(const std::string& id) const;
  // Why an edit of this node is refused ("" = allowed): the sentence for the lock tooltip.
  std::string lockReason(const std::string& id) const;

  // Runs `op` (any of the editing operations below) in dry-run mode: it is validated exactly as a real
  // call would be and its result returned, but nothing is stored and nobody is notified. The edit mode
  // uses it to show an insertion indicator only where a drop would be accepted.
  EditResult preview(const std::function<EditResult(Customization&)>& op);

  // ---- editing: visibility, labels, order -----------------------------------------------------
  EditResult setHidden(const std::string& id, bool hidden);
  EditResult hideEntry(const std::string& id) { return setHidden(id, true); }
  EditResult showEntry(const std::string& id) { return setHidden(id, false); }
  // Moves a node (with its subtree) to `to`. Rules: not across a locked node, no cycles, only into a
  // parent that may hold its kind, never between container kinds (a menu entry stays in the menu bar).
  EditResult move(const std::string& id, const Placement& to);
  // Empty (or blank) label restores the default.
  EditResult renameLabel(const std::string& id, const std::string& label);

  // ---- editing: user-created nodes ------------------------------------------------------------
  EditResult addUserMenu(const std::string& title);
  EditResult deleteUserMenu(const std::string& id);
  // `parent` may be a Section, a Menu or Submenu (its first section is used), a Toolbar or a Group.
  EditResult addCommand(const std::string& parent, const std::string& commandId, const std::string& anchor = {}, Side side = Side::End);
  EditResult addSeparator(const std::string& parent, const std::string& anchor = {}, Side side = Side::End);
  EditResult addHeading(const std::string& parent, const std::string& text, const std::string& anchor = {}, Side side = Side::End);
  EditResult addSubmenu(const std::string& parent, const std::string& title, const std::string& anchor = {}, Side side = Side::End);
  EditResult addSection(const std::string& menuOrSubmenu, const std::string& heading, const std::string& anchor = {}, Side side = Side::End);
  EditResult addSpacer(const std::string& toolbar, const std::string& anchor = {}, Side side = Side::End);
  EditResult addGroup(const std::string& toolbar, const std::vector<std::string>& commandIds, const std::string& anchor = {}, Side side = Side::End);
  // Only nodes the user created; built-in entries are hidden instead (decision D3).
  EditResult removeUserEntry(const std::string& id);
  // Clears every change inside a menu (built-in menu: also removes user entries added to it), a
  // toolbar (also its size and gap) or a panel (also snap and grid). No confirmation (spec 06 rule 29).
  EditResult resetMenu(const std::string& id);
  // Removes every user change including user menus, toolbars and panels (the widget asks first).
  EditResult resetAll();

  // ---- editing: toolbars ----------------------------------------------------------------------
  EditResult addUserToolbar(const std::string& title, Orientation orientation = Orientation::Horizontal);
  EditResult deleteUserToolbar(const std::string& id);
  EditResult setToolbarSizeStep(const std::string& toolbar, SizeStep step);
  EditResult setToolbarGap(const std::string& toolbar, double gap);

  // ---- editing: free-form panels --------------------------------------------------------------
  EditResult addUserPanel(const std::string& title, double width, double height);
  EditResult deleteUserPanel(const std::string& id);
  EditResult setPanelSnap(const std::string& panel, bool snap, double grid = kDefaultGridSize);
  // The rectangle is fitted first (grid snap when on, minimum size, inside the panel); the result
  // carries the rectangle in force.
  EditResult placeButton(const std::string& panel, const std::string& commandId, Rect rect);
  EditResult setButtonRect(const std::string& button, Rect rect);
  EditResult moveButton(const std::string& button, double x, double y);
  EditResult resizeButton(const std::string& button, double width, double height);
  EditResult deleteButton(const std::string& button);
  EditResult bringToFront(const std::string& button);
  EditResult sendToBack(const std::string& button);
  // The rectangle `rect` becomes in `panel` (snap, minimum size, clamp), without storing it.
  Rect fitRect(const std::string& panel, Rect rect) const;

  // ---- edit session (decision D6) -------------------------------------------------------------
  void beginEditSession();
  bool inEditSession() const { return session_.has_value(); }
  // Restores the user delta of the moment the session began; the session stays open. False outside one.
  bool revertSession();
  // Ends the session keeping the changes. The persistence layer saves on commit when a store is attached.
  void commitSession();
  bool sessionChanged() const { return session_ && !(*session_ == user_); }
  void setOnCommit(std::function<void()> callback) { onCommit_ = std::move(callback); }

 private:
  void changed();
  Delta merged(const Delta& user) const;
  EffectiveOptions editOptions() const;
  EditResult tryCommit(Delta candidate, const std::string& subject, EditResult success);
  std::string nextId(Delta& delta, const char* prefix) const;
  // Adds `node` to a copy of the user delta under `parent` (a Menu or Submenu parent uses its first
  // section, created when it has none) and commits it.
  EditResult addUserNode(const std::string& parent, Node node, const std::string& anchor, Side side, std::vector<std::string> groupCommands = {});
  void removeSubtree(Delta& delta, const std::string& id) const;
  const FreeFormPanelLayout* panelOfButton(const std::string& id) const;
  const ToolbarLayout* baseToolbar(const std::string& id) const;
  const FreeFormPanelLayout* basePanel(const std::string& id) const;

  LayoutSet builtin_;
  CommandExists exists_;
  std::function<std::string(const std::string&)> labeler_;
  Delta user_;
  Delta workspace_;
  uint64_t version_ = 1;
  std::optional<Delta> session_;
  std::function<void()> onCommit_;
  std::vector<std::pair<ListenerId, Listener>> listeners_;
  ListenerId nextListener_ = 1;
  bool notifying_ = false;
  bool dryRun_ = false;
  mutable std::optional<EffectiveResult> normal_;
  mutable std::optional<EffectiveResult> edit_;
};

}  // namespace r1ui::commands::customize
