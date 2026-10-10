// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of the structure half of HotkeyEditor.h: building the header, the two columns
//   and the tab pages, keeping every part in step with the registry, selection, filters, tabs, the
//   modifier layer and the detail sheet. Assignment, conflict dialogs, resets and hotkey sets are in
//   HotkeyAssign.cpp.
// Invariants: the parts are created once in onAttached and only referenced by id afterwards;
//   refreshAll is not re-entrant (a registry notification caused by the refresh is ignored); selected_
//   is empty or the id of a registered command; the keyboard, list and detail sheet always show the
//   same selected_.
// Callers: hosts, the gallery, tests.
#include "r1ui/widgets/hotkeys/HotkeyEditor.h"

#include <algorithm>

#include "HotkeyBox.h"
#include "r1ui/commands/OverrideIo.h"
#include "r1ui/commands/Overrides.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/segmented/Segmented.h"
#include "r1ui/widgets/select/Select.h"

namespace r1ui::widgets {

namespace cmd = commands;
namespace layout = core::layout;
namespace Mod = core::events::Mod;
using core::tree::WidgetId;

namespace {

constexpr const char* kAll = "*";
constexpr double kCompactWidth = 860.0;
constexpr uint8_t kModifierBits[4] = {Mod::kCtrl, Mod::kShift, Mod::kAlt, Mod::kMeta};
constexpr const char* kModifierNames[4] = {"Ctrl", "Shift", "Alt", "Meta"};

const char* kindName(cmd::CommandKind kind) {
  switch (kind) {
    case cmd::CommandKind::Action: return "Action";
    case cmd::CommandKind::Toggle: return "Toggle";
    case cmd::CommandKind::Radio: return "Radio";
    case cmd::CommandKind::Momentary: return "Momentary";
  }
  return "";
}

std::string joinChords(const std::array<cmd::ChordSequence, 2>& chords) {
  std::string text;
  for (const cmd::ChordSequence& c : chords) {
    const std::string part = cmd::formatSequence(c);
    if (part.empty()) continue;
    if (!text.empty()) text += ", ";
    text += part;
  }
  return text;
}

}  // namespace

HotkeyEditor::HotkeyEditor(CommandServices services, HotkeyEditorOptions options) : services_(services), options_(std::move(options)), setName_(options_.setName) {}

// ---- build ---------------------------------------------------------------------------------------------

void HotkeyEditor::onAttached() {
  layout::Style& s = style();
  s.direction = layout::FlexDirection::Column;
  s.gapRow = 10.0;
  s.alignItems = layout::Align::Stretch;
  s.minHeight = layout::Length::px(0);
  s.minWidth = layout::Length::px(0);
  setWantsLayoutCallback(true);
  if (setName_.empty()) setName_ = "Default";
  sets_[setName_] = cmd::exportOverrides(services_.registry, services_.overrides);

  buildHeader();
  HotkeyBox& body = hotkeyRow(ui(), id(), 14.0);
  body.style().alignItems = layout::Align::Stretch;
  body.style().flexGrow = 1.0;
  body.style().flexShrink = 1.0;
  body_ = body.id();
  buildLeft(body.id());
  buildRight(body.id());

  Label& message = ui().create<Label>(id(), "", LabelRole::Muted);
  message.style().flexShrink = 0.0;
  messageLabel_ = message.id();

  listener_ = services_.registry.subscribe([context = &ui(), self = id()] {
    HotkeyEditor* e = context->objectAs<HotkeyEditor>(self);
    if (e == nullptr || e->refreshing_) return;
    e->dirty_ = true;  // coalesced: applied by the next layout pass or flush()
    e->requestLayout();
  });
  refreshAll();
}

void HotkeyEditor::onDetached() {
  services_.registry.unsubscribe(listener_);
  listener_ = 0;
  closeConflictDialog();
  for (DialogHandle* h : {&resetDialog_, &saveDialog_}) {
    if (h->valid()) closeDialog(ui(), *h);
    *h = {};
  }
}

void HotkeyEditor::buildHeader() {
  UiContext* context = &ui();
  const WidgetId self = id();
  const auto owner = [context, self]() { return context->objectAs<HotkeyEditor>(self); };
  HotkeyBox& row = hotkeyRow(ui(), id(), 8.0);
  row.style().flexShrink = 0.0;
  ui().create<Label>(row.id(), "Hotkey Set", LabelRole::Heading);
  Select& sets = ui().create<Select>(row.id());
  sets.style().width = layout::Length::px(180);
  sets.style().flexShrink = 0.0;
  sets.setAccessibleName("Hotkey set");
  sets.setOnChanged([owner](size_t, std::string_view value) {
    if (HotkeyEditor* e = owner()) {
      if (value != e->setName_) e->loadSet(std::string(value));
    }
  });
  setSelect_ = sets.id();
  const auto makeButton = [&](HotkeyBox& parent, const char* text, const char* tip, std::function<void(HotkeyEditor&)> action) {
    Button& b = ui().create<Button>(parent.id(), text, ButtonTone::Neutral, ButtonSize::Sm);
    b.setTooltip(tip);
    b.setOnClick([owner, action = std::move(action)] {
      if (HotkeyEditor* e = owner()) action(*e);
    });
    return b.id();
  };
  saveAs_ = makeButton(row, "Save as...", "Store the current shortcuts as a named set", [](HotkeyEditor& e) { e.openSaveSetDialog(); });
  HotkeyBox& spacer = ui().create<HotkeyBox>(row.id());
  spacer.style().flexGrow = 1.0;
  import_ = makeButton(row, "Import...", "Replace the shortcuts with those of a file", [](HotkeyEditor& e) {
    if (e.onImport_) e.onImport_();
  });
  export_ = makeButton(row, "Export...", "Write the current shortcuts to a file", [](HotkeyEditor& e) {
    if (e.onExport_) e.onExport_();
  });
  resetAll_ = makeButton(row, "Reset all", "Return every shortcut to its default", [](HotkeyEditor& e) { e.requestResetAll(); });
  ui().object(import_)->setEnabled(false);
  ui().object(export_)->setEnabled(false);
}

void HotkeyEditor::buildLeft(WidgetId parent) {
  UiContext* context = &ui();
  const WidgetId self = id();
  const auto owner = [context, self]() { return context->objectAs<HotkeyEditor>(self); };
  HotkeyBox& left = hotkeyColumn(ui(), parent, 8.0);
  left.style().flexGrow = 11.0;
  left.style().flexShrink = 1.0;
  left.style().flexBasis = layout::Length::px(0);
  left.style().minWidth = layout::Length::px(380);

  HotkeyBox& filters = hotkeyRow(ui(), left.id(), 8.0);
  filters.style().flexShrink = 0.0;
  ui().create<Label>(filters.id(), "Edit hotkeys for", LabelRole::Muted);
  Select& category = ui().create<Select>(filters.id());
  category.style().flexGrow = 1.0;
  category.style().minWidth = layout::Length::px(100);
  category.setAccessibleName("Category");
  category.setOnChanged([owner](size_t, std::string_view value) {
    if (HotkeyEditor* e = owner()) e->setCategory(value == kAll ? std::string() : std::string(value));
  });
  categorySelect_ = category.id();
  Select& contexts = ui().create<Select>(filters.id());
  contexts.style().width = layout::Length::px(130);
  contexts.style().flexShrink = 0.0;
  contexts.setAccessibleName("Context");
  contexts.setOnChanged([owner](size_t, std::string_view value) {
    if (HotkeyEditor* e = owner()) e->setContextFilter(value == kAll ? std::string() : std::string(value));
  });
  contextSelect_ = contexts.id();

  ActionListOptions options;
  options.columnHeaders = true;
  options.labelHeader = "Action";
  options.descriptionHeader = "Description";
  options.shortcutHeader = "Hotkey";
  options.shortcutWidth = 150.0;
  options.showIcons = true;
  options.searchShortcuts = true;
  options.searchPlaceholder = "Enter the action name, description or hotkey...";
  ActionList& list = ui().create<ActionList>(left.id(), options);
  list.style().flexGrow = 1.0;
  list.view().setOnSelect([owner](const ActionInfo& action) {
    if (HotkeyEditor* e = owner()) e->listSelected(action);
  });
  list_ = list.id();
}

void HotkeyEditor::buildRight(WidgetId parent) {
  UiContext* context = &ui();
  const WidgetId self = id();
  const auto owner = [context, self]() { return context->objectAs<HotkeyEditor>(self); };
  HotkeyBox& right = hotkeyColumn(ui(), parent, 10.0);
  right.style().flexGrow = 9.0;
  right.style().flexShrink = 1.0;
  right.style().flexBasis = layout::Length::px(0);
  right.style().minWidth = layout::Length::px(340);

  Segmented& tabs = ui().create<Segmented>(right.id());
  tabs.style().alignSelf = layout::Align::Start;
  tabs.setItems({{"Keyboard", "", "The drawn keyboard: keys with a hotkey are highlighted", true},
                 {"Runtime Command Editor", "", "Details of the selected action; assign, clear or reset its hotkey", true}});
  tabs.setSelectedIndex(0);
  tabs.setAccessibleName("Hotkey editor pages");
  tabs.setOnChange([owner](int index) {
    if (HotkeyEditor* e = owner()) e->setTab(index == 0 ? Tab::Keyboard : Tab::Command);
  });
  tabs_ = tabs.id();

  const auto makeSlot = [&](WidgetId parentBox) {
    HotkeyBox& row = hotkeyRow(ui(), parentBox, 8.0);
    row.style().flexShrink = 0.0;
    ui().create<Label>(row.id(), "Hotkey slot", LabelRole::Muted);
    Segmented& slot = ui().create<Segmented>(row.id());
    slot.setItems({{"Primary", "", "The first shortcut of the action", true}, {"Alternate", "", "The second shortcut of the action", true}});
    slot.setSelectedIndex(0);
    slot.setAccessibleName("Hotkey slot");
    slot.setOnChange([owner](int index) {
      if (HotkeyEditor* e = owner()) e->setSlot(index);
    });
    return slot.id();
  };

  // Keyboard page.
  HotkeyBox& kb = hotkeyColumn(ui(), right.id(), 8.0);
  kb.style().flexGrow = 1.0;
  kb.style().flexShrink = 1.0;
  keyboardTab_ = kb.id();
  ui().create<Label>(kb.id(), "Press Ctrl, Shift, Alt in any combination on your keyboard (or click the modifier buttons)", LabelRole::Muted);
  ui().create<Label>(kb.id(), "to show the keys that have a hotkey using exactly those modifiers.", LabelRole::Muted);
  captionLabel_ = ui().create<Label>(kb.id(), "", LabelRole::Body).id();
  HotkeyBox& mods = hotkeyRow(ui(), kb.id(), 6.0);
  mods.style().flexShrink = 0.0;
  ui().create<Label>(mods.id(), "Modifiers", LabelRole::Muted);
  for (int i = 0; i < 4; ++i) {
    Button& b = ui().create<Button>(mods.id(), kModifierNames[i], ButtonTone::Neutral, ButtonSize::Sm);
    b.setTooltip(std::string("Show the keys used together with ") + kModifierNames[i]);
    const uint8_t bit = kModifierBits[i];
    b.setOnClick([owner, bit] {
      if (HotkeyEditor* e = owner()) e->keyboard().setToggledModifiers(static_cast<uint8_t>(e->keyboard().toggledModifiers() ^ bit));
    });
    modifierButtons_[i] = b.id();
  }
  KeyboardView& keyboard = ui().create<KeyboardView>(kb.id(), services_);
  keyboard.setOnKeyClicked([owner](cmd::Key key, uint8_t modifiers) {
    if (HotkeyEditor* e = owner()) e->keyClicked(key, modifiers);
  });
  keyboard.setOnModifiersChanged([owner](uint8_t) {
    if (HotkeyEditor* e = owner()) {
      e->refreshButtons();
      e->refreshDetail();
    }
  });
  keyboard_ = keyboard.id();
  slotKeyboard_ = makeSlot(kb.id());
  ui().create<Label>(kb.id(), "Select an action in the list, then click a key to assign it.", LabelRole::Muted);

  // Runtime command page.
  HotkeyBox& rc = hotkeyColumn(ui(), right.id(), 10.0);
  rc.style().flexGrow = 1.0;
  rc.style().flexShrink = 1.0;
  rc.style().display = layout::Display::None;
  commandTab_ = rc.id();
  CommandDetailView& detail = ui().create<CommandDetailView>(rc.id());
  detail_ = detail.id();
  slotCommand_ = makeSlot(rc.id());
  HotkeyBox& recorderRow = hotkeyRow(ui(), rc.id(), 8.0);
  recorderRow.style().flexShrink = 0.0;
  ChordRecorder& recorder = ui().create<ChordRecorder>(recorderRow.id());
  recorder.setOnChord([owner](const cmd::KeyChord& chord) {
    if (HotkeyEditor* e = owner()) e->recorded(chord);
  });
  recorder.setOnCancelled([owner] {
    if (HotkeyEditor* e = owner()) e->say("Recording cancelled");
  });
  recorder_ = recorder.id();
  HotkeyBox& buttons = hotkeyRow(ui(), rc.id(), 8.0);
  buttons.style().flexShrink = 0.0;
  const auto makeButton = [&](const char* text, const char* tip, ButtonTone tone, std::function<void(HotkeyEditor&)> action) {
    Button& b = ui().create<Button>(buttons.id(), text, tone, ButtonSize::Sm);
    b.setTooltip(tip);
    b.setOnClick([owner, action = std::move(action)] {
      if (HotkeyEditor* e = owner()) action(*e);
    });
    return b.id();
  };
  assign_ = makeButton("Assign", "Record a new shortcut for the chosen slot", ButtonTone::Accent, [](HotkeyEditor& e) { e.recorder().begin(); });
  clear_ = makeButton("Clear", "Remove the shortcut of the chosen slot", ButtonTone::Neutral, [](HotkeyEditor& e) { e.clearSlot(e.selected_, e.slot_); });
  reset_ = makeButton("Reset to default", "Return this action's shortcuts to their defaults", ButtonTone::Neutral, [](HotkeyEditor& e) { e.resetAction(e.selected_); });
}

// ---- parts -----------------------------------------------------------------------------------------------

ActionList& HotkeyEditor::list() const { return *ui().objectAs<ActionList>(list_); }
KeyboardView& HotkeyEditor::keyboard() const { return *ui().objectAs<KeyboardView>(keyboard_); }
CommandDetailView& HotkeyEditor::detailView() const { return *ui().objectAs<CommandDetailView>(detail_); }
ChordRecorder& HotkeyEditor::recorder() const { return *ui().objectAs<ChordRecorder>(recorder_); }

WidgetId HotkeyEditor::modifierButton(uint8_t bit) const {
  for (int i = 0; i < 4; ++i) {
    if (kModifierBits[i] == bit) return modifierButtons_[i];
  }
  return {};
}

void HotkeyEditor::setOnImport(std::function<void()> callback) {
  onImport_ = std::move(callback);
  if (WidgetObject* b = ui().object(import_)) b->setEnabled(static_cast<bool>(onImport_));
}

void HotkeyEditor::setOnExport(std::function<void()> callback) {
  onExport_ = std::move(callback);
  if (WidgetObject* b = ui().object(export_)) b->setEnabled(static_cast<bool>(onExport_));
}

void HotkeyEditor::say(std::string text) {
  message_ = std::move(text);
  if (Label* label = ui().objectAs<Label>(messageLabel_)) label->setText(message_);
}

// ---- refresh --------------------------------------------------------------------------------------------------

void HotkeyEditor::refreshSelects() {
  std::vector<std::string> categories = services_.registry.categories();
  if (Select* select = ui().objectAs<Select>(categorySelect_); select != nullptr && categories != categoryValues_) {
    categoryValues_ = categories;
    std::vector<SelectEntry> entries{{SelectEntryKind::Item, "All categories", kAll, false}};
    for (const std::string& c : categories) entries.push_back({SelectEntryKind::Item, c, c, false});
    select->setEntries(std::move(entries));
  }
  std::vector<std::string> contexts;
  for (const cmd::ContextInfo& c : services_.registry.contexts()) contexts.push_back(c.name);
  if (Select* select = ui().objectAs<Select>(contextSelect_); select != nullptr && contexts != contextValues_) {
    contextValues_ = contexts;
    std::vector<SelectEntry> entries{{SelectEntryKind::Item, "All contexts", kAll, false}};
    for (const std::string& c : contexts) entries.push_back({SelectEntryKind::Item, c, c, false});
    select->setEntries(std::move(entries));
  }
  if (Select* select = ui().objectAs<Select>(categorySelect_)) select->setSelectedValue(category_.empty() ? kAll : category_);
  if (Select* select = ui().objectAs<Select>(contextSelect_)) select->setSelectedValue(context_.empty() ? kAll : context_);
  refreshSetSelect();
}

void HotkeyEditor::refreshSetSelect() {
  Select* select = ui().objectAs<Select>(setSelect_);
  if (select == nullptr) return;
  std::vector<SelectEntry> entries;
  for (const auto& entry : sets_) entries.push_back({SelectEntryKind::Item, entry.first, entry.first, false});
  select->setEntries(std::move(entries));
  select->setSelectedValue(setName_);
}

void HotkeyEditor::flush() {
  if (dirty_) refreshAll();
}

void HotkeyEditor::refreshAll() {
  if (refreshing_) return;
  refreshing_ = true;
  dirty_ = false;
  refreshSelects();
  std::vector<ActionInfo> actions = actionsFromRegistry(services_.registry, services_.keymap, {false, true, true});
  if (!context_.empty()) {
    const std::vector<std::string> chain = services_.registry.contextChain(context_);
    actions.erase(std::remove_if(actions.begin(), actions.end(),
                                 [&](const ActionInfo& a) {
                                   const cmd::CommandDef* def = services_.registry.find(a.id);
                                   return def == nullptr || std::find(chain.begin(), chain.end(), def->context) == chain.end();
                                 }),
                  actions.end());
  }
  list().setActions(std::move(actions));
  list().setCategoryFilter(category_);
  keyboard().setUsageFilter({category_, context_});
  keyboard().refresh();
  if (!selected_.empty() && services_.registry.find(selected_) == nullptr) selected_.clear();
  keyboard().setSelectedCommand(selected_);
  refreshDetail();
  refreshButtons();
  refreshing_ = false;
}

void HotkeyEditor::refreshDetail() {
  CommandDetail detail;
  const cmd::CommandDef* def = selected_.empty() ? nullptr : services_.registry.find(selected_);
  if (def != nullptr) {
    detail.valid = true;
    detail.id = def->id;
    detail.label = def->label;
    detail.description = def->description;
    detail.category = cmd::categoryOf(*def);
    detail.context = def->context;
    detail.kind = kindName(def->kind);
    detail.defaults = joinChords(def->defaultChords);
    detail.enabled = def->isEnabled();
    std::array<cmd::ChordSequence, 2> current{};
    for (int slot = 0; slot < cmd::kSlotCount; ++slot) {
      const std::optional<cmd::ChordSequence> chord = services_.keymap.effective(def->id, slot);
      if (!chord || chord->empty()) continue;
      current[static_cast<size_t>(slot)] = *chord;
      for (const cmd::Conflict& c : cmd::findConflicts(services_.registry, services_.keymap, def->id, *chord)) {
        const cmd::CommandDef* other = services_.registry.find(c.commandId);
        detail.conflicts.push_back(cmd::formatSequence(*chord) + " is also used by " + (other != nullptr ? other->label : c.commandId) + " [" + c.context + "]");
      }
    }
    detail.current = joinChords(current);
  }
  detailView().setDetail(std::move(detail));
  std::string idle;
  if (def != nullptr) {
    const std::optional<cmd::ChordSequence> chord = services_.keymap.effective(def->id, slot_);
    if (chord && !chord->empty()) idle = cmd::formatSequence(*chord);
  }
  recorder().setIdleText(std::move(idle));
  if (Label* label = ui().objectAs<Label>(captionLabel_)) label->setText(caption());
}

void HotkeyEditor::refreshButtons() {
  const cmd::CommandDef* def = selected_.empty() ? nullptr : services_.registry.find(selected_);
  const bool has = def != nullptr;
  bool hasChord = false;
  bool hasOverride = false;
  if (has) {
    const std::optional<cmd::ChordSequence> chord = services_.keymap.effective(def->id, slot_);
    hasChord = chord && !chord->empty();
    hasOverride = services_.overrides.find(def->id, 0) != nullptr || services_.overrides.find(def->id, 1) != nullptr;
  }
  if (WidgetObject* b = ui().object(assign_)) b->setEnabled(has);
  if (WidgetObject* b = ui().object(clear_)) b->setEnabled(hasChord);
  if (WidgetObject* b = ui().object(reset_)) b->setEnabled(hasOverride);
  const uint8_t layer = keyboard().effectiveModifiers();
  for (int i = 0; i < 4; ++i) {
    if (Button* b = ui().objectAs<Button>(modifierButtons_[i])) b->setTone((layer & kModifierBits[i]) != 0 ? ButtonTone::Accent : ButtonTone::Neutral);
  }
  if (Label* label = ui().objectAs<Label>(captionLabel_)) label->setText(caption());
}

std::string HotkeyEditor::caption() const {
  std::string text = "Currently displaying hotkeys for: " + (category_.empty() ? std::string("all categories") : category_);
  if (!context_.empty()) text += " in " + context_;
  std::string layer = modifierPrefix(keyboard().effectiveModifiers());
  if (!layer.empty()) layer.pop_back();
  text += layer.empty() ? "  (no modifiers)" : "  (" + layer + ")";
  return text;
}

// ---- selection, filters, tabs ----------------------------------------------------------------------------------

void HotkeyEditor::listSelected(const ActionInfo& action) {
  if (services_.registry.find(action.id) == nullptr) return;
  selected_ = action.id;
  keyboard().setSelectedCommand(selected_);
  // Show the layer the action's own shortcut lives in.
  for (int slot = 0; slot < cmd::kSlotCount; ++slot) {
    const std::optional<cmd::ChordSequence> chord = services_.keymap.effective(selected_, slot);
    if (chord && !chord->empty()) {
      keyboard().setToggledModifiers(chord->first().modifiers);
      break;
    }
  }
  refreshDetail();
  refreshButtons();
}

bool HotkeyEditor::selectAction(std::string_view commandId) {
  flush();
  const cmd::CommandDef* def = services_.registry.find(commandId);
  if (def == nullptr || def->hiddenFromEditor) return false;
  if (!context_.empty()) {
    const std::vector<std::string> chain = services_.registry.contextChain(context_);
    if (std::find(chain.begin(), chain.end(), def->context) == chain.end()) return false;
  }
  if (!category_.empty() && cmd::categoryOf(*def) != category_) setCategory({});
  ActionListView& view = list().view();
  if (!view.selectAction(commandId)) {
    list().setFilter({});
    if (!view.selectAction(commandId)) return false;
  }
  const ActionInfo* action = view.selectedAction();
  if (action != nullptr) listSelected(*action);
  return selected_ == commandId;
}

void HotkeyEditor::setCategory(std::string category) {
  if (category == category_) return;
  category_ = std::move(category);
  if (Select* select = ui().objectAs<Select>(categorySelect_)) select->setSelectedValue(category_.empty() ? kAll : category_);
  list().setCategoryFilter(category_);
  keyboard().setUsageFilter({category_, context_});
  refreshDetail();
}

void HotkeyEditor::setContextFilter(std::string context) {
  if (context == context_) return;
  context_ = std::move(context);
  if (Select* select = ui().objectAs<Select>(contextSelect_)) select->setSelectedValue(context_.empty() ? kAll : context_);
  refreshAll();
}

void HotkeyEditor::setFilter(const std::string& text) { list().setFilter(text); }

void HotkeyEditor::setTab(Tab tab) {
  if (tab == tab_) return;
  tab_ = tab;
  if (Segmented* tabs = ui().objectAs<Segmented>(tabs_)) tabs->setSelectedIndex(tab == Tab::Keyboard ? 0 : 1);
  const auto show = [&](WidgetId page, bool on) {
    if (WidgetObject* o = ui().object(page)) {
      o->style().display = on ? layout::Display::Flex : layout::Display::None;
      o->requestLayout();
    }
  };
  show(keyboardTab_, tab == Tab::Keyboard);
  show(commandTab_, tab == Tab::Command);
  if (tab == Tab::Keyboard) recorder().cancel();
}

void HotkeyEditor::setSlot(int slot) {
  if (slot < 0 || slot >= cmd::kSlotCount || slot == slot_) return;
  slot_ = slot;
  for (const WidgetId w : {slotKeyboard_, slotCommand_}) {
    if (Segmented* s = ui().objectAs<Segmented>(w)) s->setSelectedIndex(slot);
  }
  refreshDetail();
  refreshButtons();
}

std::string HotkeyEditor::slotName(int slot) const { return slot == 0 ? "primary" : "alternate"; }

// ---- keyboard and recorder paths -------------------------------------------------------------------------------

void HotkeyEditor::keyClicked(cmd::Key key, uint8_t modifiers) {
  flush();
  const cmd::KeyChord chord{key, modifiers, false};
  if (!chord.valid()) return;
  if (!selected_.empty()) {
    assign(selected_, slot_, cmd::ChordSequence::single(chord));
    return;
  }
  const std::vector<cmd::KeyUse>& uses = keyboard().usesOf(key);
  if (uses.empty()) {
    say(cmd::formatChord(chord) + " is unassigned. Select an action, then click a key to assign it.");
    return;
  }
  const std::string first = uses.front().commandId;
  if (!selectAction(first)) {
    say(cmd::formatChord(chord) + " is used by " + first);
    return;
  }
  const cmd::CommandDef* def = services_.registry.find(first);
  say(cmd::formatChord(chord) + " runs " + (def != nullptr ? def->label : first) + (uses.size() > 1 ? " (and " + std::to_string(uses.size() - 1) + " more)" : std::string()));
}

void HotkeyEditor::recorded(const cmd::KeyChord& chord) {
  if (selected_.empty()) return;
  assign(selected_, slot_, cmd::ChordSequence::single(chord));
}

// Key events that bubble to the editor carry the physical modifier state.
void HotkeyEditor::onKeyDown(Event& e) { keyboard().setPhysicalModifiers(e.modifiers); }
void HotkeyEditor::onKeyUp(Event& e) { keyboard().setPhysicalModifiers(e.modifiers); }

void HotkeyEditor::onLayout() {
  flush();
  const bool compact = ui().absRect(id()).w > 0.0f && ui().absRect(id()).w < kCompactWidth;
  if (compact == compact_) return;
  compact_ = compact;
  if (WidgetObject* body = ui().object(body_)) {
    body->style().direction = compact ? layout::FlexDirection::Column : layout::FlexDirection::Row;
    body->requestLayout();
  }
}

}  // namespace r1ui::widgets
