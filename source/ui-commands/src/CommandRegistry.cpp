// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CommandRegistry.h: validation and sanitising of declarations, the context
//   tree, ordered enumeration and change notification.
// Invariants: every stored command has a valid unique id, a non-empty sanitised label, a known
//   context, valid default chords and a registration serial; ordered_ holds the commands sorted by
//   serial whenever orderedDirty_ is false; the version only grows.
// Callers: modules, Keymap, CommandRouter, widgets.
#include "r1ui/commands/CommandRegistry.h"

#include <algorithm>
#include <set>

#include "r1ui/commands/Text.h"

namespace r1ui::commands {

namespace {

constexpr const char* kDefaultCategory = "General";

RegisterResult refuse(RegisterError error, std::string message) { return {false, error, std::move(message)}; }

bool blank(const std::string& text) {
  return std::all_of(text.begin(), text.end(), [](char c) { return c == ' '; });
}

}  // namespace

CommandRegistry::CommandRegistry() {
  contexts_.push_back({kGlobalContext, {}, false, "Application-wide shortcuts"});
  contexts_.push_back({kWindowContext, kGlobalContext, false, "Main window"});
  contexts_.push_back({kTextContext, kWindowContext, true, "Text editing"});
  for (size_t i = 0; i < contexts_.size(); ++i) contextIndex_.emplace(contexts_[i].name, i);
}

// ---- contexts -----------------------------------------------------------------------------------

bool CommandRegistry::addContext(std::string name, std::string_view parent, bool textEntry, std::string description) {
  if (!isValidIdentifier(name, kMaxIdBytes) || contextIndex_.contains(name) || contexts_.size() >= kMaxContexts) return false;
  if (!contextIndex_.contains(std::string(parent))) return false;
  if (static_cast<int>(contextChain(parent).size()) >= kMaxContextDepth) return false;
  contextIndex_.emplace(name, contexts_.size());
  contexts_.push_back({std::move(name), std::string(parent), textEntry, sanitizeText(description, kMaxDescriptionBytes)});
  touch();
  return true;
}

const ContextInfo* CommandRegistry::context(std::string_view name) const {
  const auto it = contextIndex_.find(std::string(name));
  return it == contextIndex_.end() ? nullptr : &contexts_[it->second];
}

std::vector<std::string> CommandRegistry::contextChain(std::string_view name) const {
  std::vector<std::string> chain;
  for (const ContextInfo* c = context(name); c != nullptr && chain.size() <= static_cast<size_t>(kMaxContextDepth); c = context(c->parent)) {
    chain.push_back(c->name);
  }
  return chain;
}

bool CommandRegistry::isAncestor(std::string_view ancestor, std::string_view descendant) const {
  if (ancestor == descendant) return false;
  const std::vector<std::string> chain = contextChain(descendant);
  return std::find(chain.begin(), chain.end(), ancestor) != chain.end();
}

std::vector<std::string> CommandRegistry::descendantsOf(std::string_view name) const {
  std::vector<std::string> out;
  for (const ContextInfo& c : contexts_) {
    if (isAncestor(name, c.name)) out.push_back(c.name);
  }
  return out;
}

// ---- commands -----------------------------------------------------------------------------------

RegisterResult CommandRegistry::add(CommandDef def) {
  if (!isValidIdentifier(def.id, kMaxIdBytes)) return refuse(RegisterError::InvalidId, "command id must be 1..128 characters of [A-Za-z0-9._:/-]");
  if (commands_.contains(def.id)) return refuse(RegisterError::DuplicateId, "command id already registered: " + def.id);
  if (commands_.size() >= kMaxCommands) return refuse(RegisterError::TooManyCommands, "too many commands");
  def.label = sanitizeText(def.label, kMaxLabelBytes);
  if (blank(def.label)) return refuse(RegisterError::EmptyLabel, "command label must not be empty: " + def.id);
  if (!def.icon.empty() && !isValidIconName(def.icon)) return refuse(RegisterError::InvalidIcon, "invalid icon name for " + def.id);
  if (def.context.empty()) def.context = kGlobalContext;
  if (context(def.context) == nullptr) return refuse(RegisterError::UnknownContext, "unknown context " + def.context + " for " + def.id);
  def.category = sanitizeText(def.category, kMaxCategoryBytes);
  if (blank(def.category)) def.category = kDefaultCategory;
  if (def.kind == CommandKind::Radio && !isValidIdentifier(def.radioGroup, kMaxIdBytes)) {
    return refuse(RegisterError::MissingRadioGroup, "a radio command needs a radio group: " + def.id);
  }
  for (const ChordSequence& chord : def.defaultChords) {
    if (!chord.empty() && !chord.valid()) return refuse(RegisterError::InvalidChord, "invalid default chord for " + def.id);
  }
  def.description = sanitizeText(def.description, kMaxDescriptionBytes);
  def.tooltip = sanitizeText(def.tooltip, kMaxDescriptionBytes);

  auto entry = std::make_unique<Entry>();
  entry->serial = nextSerial_++;
  entry->def = std::move(def);
  const std::string id = entry->def.id;
  commands_.emplace(id, std::move(entry));
  orderedDirty_ = true;
  touch();
  return {true, RegisterError::None, {}};
}

bool CommandRegistry::remove(std::string_view id) {
  const auto it = commands_.find(std::string(id));
  if (it == commands_.end()) return false;
  commands_.erase(it);
  orderedDirty_ = true;
  touch();
  return true;
}

const CommandDef* CommandRegistry::find(std::string_view id) const {
  const auto it = commands_.find(std::string(id));
  return it == commands_.end() ? nullptr : &it->second->def;
}

uint64_t CommandRegistry::serialOf(std::string_view id) const {
  const auto it = commands_.find(std::string(id));
  return it == commands_.end() ? 0 : it->second->serial;
}

const std::vector<const CommandDef*>& CommandRegistry::commands() const {
  if (orderedDirty_) {
    std::vector<const Entry*> entries;
    entries.reserve(commands_.size());
    for (const auto& pair : commands_) entries.push_back(pair.second.get());
    std::sort(entries.begin(), entries.end(), [](const Entry* a, const Entry* b) { return a->serial < b->serial; });
    ordered_.clear();
    ordered_.reserve(entries.size());
    for (const Entry* e : entries) ordered_.push_back(&e->def);
    orderedDirty_ = false;
  }
  return ordered_;
}

std::vector<std::string> CommandRegistry::categories() const {
  std::set<std::string> distinct;
  for (const CommandDef* c : commands()) distinct.insert(c->category);
  return {distinct.begin(), distinct.end()};
}

std::vector<const CommandDef*> CommandRegistry::inCategory(std::string_view category) const {
  std::vector<const CommandDef*> out;
  for (const CommandDef* c : commands()) {
    if (c->category == category) out.push_back(c);
  }
  return out;
}

std::vector<const CommandDef*> CommandRegistry::inRadioGroup(std::string_view group) const {
  std::vector<const CommandDef*> out;
  for (const CommandDef* c : commands()) {
    if (c->kind == CommandKind::Radio && c->radioGroup == group) out.push_back(c);
  }
  return out;
}

// ---- change tracking ----------------------------------------------------------------------------

void CommandRegistry::touch() {
  ++version_;
  notify();
}

// Listeners run on a copy of the list so they may subscribe or unsubscribe; a touch() made by a
// listener only bumps the version and asks for another round (bounded by kMaxNotifyRounds).
void CommandRegistry::notify() {
  if (notifying_) {
    renotify_ = true;
    return;
  }
  notifying_ = true;
  for (int round = 0; round < kMaxNotifyRounds; ++round) {
    renotify_ = false;
    const auto snapshot = listeners_;
    for (const auto& [id, listener] : snapshot) {
      const bool stillThere = std::any_of(listeners_.begin(), listeners_.end(), [&](const auto& l) { return l.first == id; });
      if (stillThere && listener) listener();
    }
    if (!renotify_) break;
  }
  notifying_ = false;
}

CommandRegistry::ListenerId CommandRegistry::subscribe(std::function<void()> listener) {
  if (!listener) return 0;
  const ListenerId id = nextListener_++;
  listeners_.emplace_back(id, std::move(listener));
  return id;
}

void CommandRegistry::unsubscribe(ListenerId id) {
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [&](const auto& l) { return l.first == id; }), listeners_.end());
}

}  // namespace r1ui::commands
