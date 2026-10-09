// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CommandRegistry: the set of declared commands, the tree of binding contexts they belong to,
//   a version counter and change notification.
// Why: spec 07 rules 1 to 6 and 11: one declaration per id, a duplicate or an empty label is a
//   developer error and the declaration is refused, a removed command frees its chords. Contexts
//   (global, window, panel types, text editing) are arranged as a parent chain so the same chord can
//   mean different commands in different panels (rules 17, 22 to 25); each command belongs to exactly
//   one context.
// Callers: modules (add / remove commands, add contexts), Keymap and CommandRouter (read), the
//   overrides (touch after a binding change), widgets (subscribe to refresh menus, toolbars and the
//   editor). Calls: Command.h, Text.h.
// Built-in contexts: "global" (root), "window" (child of global) and "text" (child of window, flagged
//   text-entry: it swallows plain printable keys, see CommandRouter). Modules add their own below them.
// Version: bumped by every successful add / remove / addContext and by touch(); listeners run after
//   the change, outside any internal iteration, so they may read the registry or call touch(); a
//   listener that keeps touching is cut off after kMaxNotifyRounds rounds. Pointers returned by find()
//   and commands() stay valid until that command is removed.
// Failure behavior: nothing throws; add() reports why a declaration was refused and keeps the
//   previous state. Limits: kMaxCommands commands, kMaxContexts contexts, context depth kMaxContextDepth.
// Threading: UI thread only.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "r1ui/commands/Command.h"

namespace r1ui::commands {

inline constexpr size_t kMaxContexts = 4096;
inline constexpr int kMaxContextDepth = 64;
inline constexpr int kMaxNotifyRounds = 8;
inline constexpr const char* kWindowContext = "window";
inline constexpr const char* kTextContext = "text";

enum class RegisterError : uint8_t {
  None,
  InvalidId,
  DuplicateId,
  EmptyLabel,
  InvalidIcon,
  InvalidCategory,
  UnknownContext,
  InvalidChord,
  MissingRadioGroup,
  TooManyCommands
};

struct RegisterResult {
  bool ok = false;
  RegisterError error = RegisterError::None;
  std::string message;
  explicit operator bool() const { return ok; }
};

struct ContextInfo {
  std::string name;
  std::string parent;  // empty for "global"
  bool textEntry = false;
  std::string description;
};

class CommandRegistry {
 public:
  using ListenerId = uint32_t;

  CommandRegistry();

  // ---- contexts ----
  // Adds a context below an existing parent. False for a bad or duplicate name, an unknown parent,
  // a chain deeper than kMaxContextDepth or too many contexts.
  bool addContext(std::string name, std::string_view parent = kGlobalContext, bool textEntry = false, std::string description = {});
  const ContextInfo* context(std::string_view name) const;
  // The context followed by its parents up to "global"; empty for an unknown name.
  std::vector<std::string> contextChain(std::string_view name) const;
  // True when `ancestor` is a proper ancestor of `descendant`.
  bool isAncestor(std::string_view ancestor, std::string_view descendant) const;
  // Direct and indirect descendants of `name`, in creation order.
  std::vector<std::string> descendantsOf(std::string_view name) const;
  const std::vector<ContextInfo>& contexts() const { return contexts_; }

  // ---- commands ----
  RegisterResult add(CommandDef def);
  bool remove(std::string_view id);
  const CommandDef* find(std::string_view id) const;
  size_t size() const { return commands_.size(); }
  // All commands in registration order.
  const std::vector<const CommandDef*>& commands() const;
  // Sorted distinct categories ("General" for commands without one).
  std::vector<std::string> categories() const;
  std::vector<const CommandDef*> inCategory(std::string_view category) const;
  std::vector<const CommandDef*> inRadioGroup(std::string_view group) const;
  // Registration serial of a command (increases with every add; 0 for an unknown id).
  uint64_t serialOf(std::string_view id) const;

  // ---- change tracking ----
  uint64_t version() const { return version_; }
  // Bumps the version and notifies: hosts call it when command state changed in a way the
  // predicates alone do not announce, the overrides call it after a binding change.
  void touch();
  ListenerId subscribe(std::function<void()> listener);
  void unsubscribe(ListenerId id);

 private:
  struct Entry {
    CommandDef def;
    uint64_t serial = 0;
  };
  void notify();

  std::vector<ContextInfo> contexts_;
  std::unordered_map<std::string, size_t> contextIndex_;
  std::unordered_map<std::string, std::unique_ptr<Entry>> commands_;
  mutable std::vector<const CommandDef*> ordered_;
  mutable bool orderedDirty_ = false;
  uint64_t nextSerial_ = 1;
  uint64_t version_ = 1;
  std::vector<std::pair<ListenerId, std::function<void()>>> listeners_;
  ListenerId nextListener_ = 1;
  bool notifying_ = false;
  bool renotify_ = false;
};

}  // namespace r1ui::commands
