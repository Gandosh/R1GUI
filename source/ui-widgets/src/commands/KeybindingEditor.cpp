// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of the structure half of KeybindingEditor.h: the header controls, the grouped
//   rows, the filters, text refresh from the keymap, reset actions and the structure queries. The
//   capture state machine and the conflict popup are in KeybindingCapture.cpp.
// Invariants: rows_ and headings_ mirror the widgets under the scroll area's content; signature_ is the
//   command set the rows were built for; a change of the command set rebuilds (and cancels a capture),
//   anything else only refreshes box texts; a row hidden by a filter is display-none and its
//   boxes keep their widgets.
// Callers: hosts, GalleryCommands, tests.
#include "r1ui/widgets/commands/KeybindingEditor.h"

#include <algorithm>
#include <set>

#include "LayoutBox.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/checkbox/Checkbox.h"
#include "r1ui/widgets/commands/KeybindingFilter.h"
#include "r1ui/widgets/iconbutton/IconButton.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/scroll/ScrollArea.h"
#include "r1ui/widgets/select/Select.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace r1ui::widgets {

namespace cmd = commands;
namespace layout = core::layout;
using core::tree::WidgetId;

namespace {

constexpr const char* kAllContexts = "*";
constexpr double kRowPadding = 6.0;
constexpr double kRowGap = 8.0;

std::string lowered(std::string text) {
  for (char& c : text) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  return text;
}

}  // namespace

KeybindingEditor::KeybindingEditor(CommandServices services, KeybindingEditorOptions options)
    : services_(services), options_(options), twoStep_(options.twoStepCapture) {}

// ---- build --------------------------------------------------------------------------------------

void KeybindingEditor::onAttached() {
  layout::Style& s = style();
  s.direction = layout::FlexDirection::Column;
  s.gapRow = 8.0;
  s.minHeight = layout::Length::px(0);
  buildHeader();
  ScrollArea& scroll = ui().create<ScrollArea>(id());
  scroll.style().flexGrow = 1.0;
  scroll.style().flexShrink = 1.0;
  scroll.style().minHeight = layout::Length::px(0);
  scroll_ = scroll.id();
  buildRows();
  listener_ = services_.registry.subscribe([this] { onRegistryChanged(); });
}

void KeybindingEditor::onDetached() {
  services_.registry.unsubscribe(listener_);
  listener_ = 0;
  cancelCapture();
}

void KeybindingEditor::buildHeader() {
  UiContext* context = &ui();
  const WidgetId self = id();
  const auto owner = [context, self]() { return context->objectAs<KeybindingEditor>(self); };

  CommandsLayoutBox& top = layoutRow(ui(), id(), kRowGap);
  top.style().flexShrink = 0.0;
  TextInput& search = ui().create<TextInput>(top.id());
  search.style().flexGrow = 1.0;
  search.setPlaceholder("Search commands, categories or shortcuts");
  search.setClearable(true);
  search.setAccessibleName("Search shortcuts");
  search.setOnTextChanged([owner](std::string_view text) {
    if (KeybindingEditor* e = owner()) e->setFilter(std::string(text));
  });
  search_ = search.id();

  Select& select = ui().create<Select>(top.id());
  select.style().width = layout::Length::px(150);
  select.style().flexShrink = 0.0;
  std::vector<SelectEntry> entries{{SelectEntryKind::Item, "All contexts", kAllContexts, false}};
  for (const cmd::ContextInfo& c : services_.registry.contexts()) entries.push_back({SelectEntryKind::Item, c.name, c.name, false});
  select.setEntries(std::move(entries));
  select.setSelectedIndex(0);
  select.setAccessibleName("Context");
  select.setOnChanged([owner](size_t, std::string_view value) {
    if (KeybindingEditor* e = owner()) e->setContextFilter(value == kAllContexts ? std::string() : std::string(value));
  });
  contextSelect_ = select.id();

  Checkbox& twoStep = ui().create<Checkbox>(top.id(), "Two-step");
  twoStep.setChecked(twoStep_);
  twoStep.setTooltip("Capture shortcuts of two key presses (press Enter after the first to keep one)");
  twoStep.setOnChange([owner](bool on) {
    if (KeybindingEditor* e = owner()) e->setTwoStepCapture(on);
  });
  twoStepBox_ = twoStep.id();

  CommandsLayoutBox& actions = layoutRow(ui(), id(), kRowGap);
  actions.style().justifyContent = layout::Justify::End;
  actions.style().flexShrink = 0.0;
  const auto makeButton = [&](const char* text, const char* tip, std::function<void(KeybindingEditor&)> action) {
    Button& b = ui().create<Button>(actions.id(), text, ButtonTone::Neutral, ButtonSize::Sm);
    b.setTooltip(tip);
    b.setOnClick([owner, action = std::move(action)] {
      if (KeybindingEditor* e = owner()) action(*e);
    });
    return b.id();
  };
  import_ = makeButton("Import...", "Replace the shortcuts with those of a file", [](KeybindingEditor& e) {
    if (e.onImport_) e.onImport_();
  });
  export_ = makeButton("Export...", "Write the current shortcuts to a file", [](KeybindingEditor& e) {
    if (e.onExport_) e.onExport_();
  });
  resetAll_ = makeButton("Reset all", "Return every shortcut to its default", [](KeybindingEditor& e) { e.requestResetAll(); });
  ui().object(import_)->setEnabled(false);
  ui().object(export_)->setEnabled(false);
}

void KeybindingEditor::clearRows() {
  if (ScrollArea* scroll = ui().objectAs<ScrollArea>(scroll_)) {
    std::vector<WidgetId> children;
    for (WidgetId c = ui().tree().firstChild(scroll->content()); c.valid(); c = ui().tree().nextSibling(c)) children.push_back(c);
    for (const WidgetId c : children) ui().destroy(c);
  }
  rows_.clear();
  headings_.clear();
}

uint64_t KeybindingEditor::commandSignature() const {
  uint64_t signature = services_.registry.size();
  for (const cmd::CommandDef* c : services_.registry.commands()) {
    if (c->hiddenFromEditor) continue;
    signature = (signature ^ services_.registry.serialOf(c->id)) * 1099511628211ULL;
  }
  return signature;
}

void KeybindingEditor::buildRows() {
  clearRows();
  ScrollArea* scroll = ui().objectAs<ScrollArea>(scroll_);
  if (scroll == nullptr) return;
  std::set<std::string> categories;
  std::vector<const cmd::CommandDef*> visible;
  for (const cmd::CommandDef* c : services_.registry.commands()) {
    if (c->hiddenFromEditor) continue;
    visible.push_back(c);
    categories.insert(c->category);
  }
  try {
    for (const std::string& category : categories) {
      std::vector<const cmd::CommandDef*> inCategory;
      for (const cmd::CommandDef* c : visible) {
        if (c->category == category) inCategory.push_back(c);
      }
      std::stable_sort(inCategory.begin(), inCategory.end(), [](const cmd::CommandDef* a, const cmd::CommandDef* b) { return lowered(a->label) < lowered(b->label); });

      Label& heading = ui().create<Label>(scroll->content(), category, LabelRole::Heading);
      heading.style().margin[layout::kTop] = layout::Length::px(8);
      heading.style().margin[layout::kBottom] = layout::Length::px(2);
      headings_.push_back({heading.id(), true});
      const size_t headingIndex = headings_.size() - 1;

      for (const cmd::CommandDef* c : inCategory) {
        Row row;
        row.commandId = c->id;
        row.label = c->label;
        row.category = c->category;
        row.context = c->context;
        row.heading = headingIndex;
        CommandsLayoutBox& line = layoutRow(ui(), scroll->content(), kRowGap);
        line.style().flexShrink = 0.0;
        line.style().minHeight = layout::Length::px(40);
        line.style().padding[layout::kTop] = line.style().padding[layout::kBottom] = kRowPadding;
        row.row = line.id();

        CommandsLayoutBox& names = layoutColumn(ui(), line.id(), 2);
        names.style().alignItems = layout::Align::Start;
        names.style().flexGrow = 1.0;
        names.style().flexShrink = 1.0;
        names.style().minWidth = layout::Length::px(options_.nameMinWidth);
        ui().create<Label>(names.id(), c->label, LabelRole::Body);
        if (!c->description.empty()) ui().create<Label>(names.id(), c->description, LabelRole::Muted);

        Label& context = ui().create<Label>(line.id(), c->context, LabelRole::Caption);
        context.style().width = layout::Length::px(options_.contextWidth);
        context.style().flexShrink = 0.0;

        for (int slot = 0; slot < cmd::kSlotCount; ++slot) {
          ChordBox& box = ui().create<ChordBox>(line.id(), id(), c->id, slot);
          box.style().width = layout::Length::px(options_.boxWidth);
          row.boxes[slot] = box.id();
        }
        IconButton& reset = ui().create<IconButton>(line.id(), "undo2", IconButtonSize::Sm);
        reset.setTooltip("Reset to default");
        reset.setAccessibleName("Reset " + c->label);
        reset.setOnClick([context = &ui(), self = id(), commandId = c->id] {
          if (KeybindingEditor* e = context->objectAs<KeybindingEditor>(self)) e->resetCommand(commandId);
        });
        row.reset = reset.id();
        rows_.push_back(std::move(row));
      }
    }
  } catch (const std::length_error&) {
    // The widget tree is full: the rows that fit stay (a documented limit of the tree, not a failure).
  }
  signature_ = commandSignature();
  refreshTexts();
  applyFilters();
}

// ---- live texts and filters ---------------------------------------------------------------------

std::string KeybindingEditor::chordTextOf(const std::string& commandId, int slot) const {
  const std::optional<cmd::ChordSequence> chord = services_.keymap.effective(commandId, slot);
  return chord ? cmd::formatSequence(*chord) : std::string();
}

std::string KeybindingEditor::hintOf(const std::string& commandId, int slot) const {
  const cmd::CommandDef* command = services_.registry.find(commandId);
  if (command != nullptr && !command->defaultChords[static_cast<size_t>(slot)].empty()) return cmd::formatSequence(command->defaultChords[static_cast<size_t>(slot)]);
  return "Press a key combination";
}

void KeybindingEditor::refreshTexts() {
  for (const Row& row : rows_) {
    for (int slot = 0; slot < cmd::kSlotCount; ++slot) {
      if (ChordBox* box = ui().objectAs<ChordBox>(row.boxes[slot])) {
        box->setChordText(chordTextOf(row.commandId, slot));
        box->setHint(hintOf(row.commandId, slot));
      }
    }
    const bool overridden = services_.overrides.find(row.commandId, 0) != nullptr || services_.overrides.find(row.commandId, 1) != nullptr;
    if (WidgetObject* reset = ui().object(row.reset)) reset->setEnabled(overridden);
  }
}

void KeybindingEditor::onRegistryChanged() {
  if (commandSignature() != signature_) {
    cancelCapture();
    buildRows();
    return;
  }
  refreshTexts();
}

void KeybindingEditor::applyFilters() {
  std::vector<bool> headingShown(headings_.size(), false);
  for (Row& row : rows_) {
    const std::string c0 = chordTextOf(row.commandId, 0);
    const std::string c1 = chordTextOf(row.commandId, 1);
    const bool show = matchesKeybindingQuery(filter_, {row.label, row.category, row.commandId, c0, c1}) && (contextFilter_.empty() || row.context == contextFilter_);
    if (WidgetObject* line = ui().object(row.row)) {
      const layout::Display wanted = show ? layout::Display::Flex : layout::Display::None;
      if (line->style().display != wanted) {
        line->style().display = wanted;
        line->requestLayout();
      }
    }
    row.visible = show;
    if (show) headingShown[row.heading] = true;
    if (!show && capture_.active && capture_.commandId == row.commandId) cancelCapture();
  }
  for (size_t i = 0; i < headings_.size(); ++i) {
    headings_[i].visible = headingShown[i];
    if (WidgetObject* h = ui().object(headings_[i].widget)) {
      const layout::Display wanted = headingShown[i] ? layout::Display::Flex : layout::Display::None;
      if (h->style().display != wanted) {
        h->style().display = wanted;
        h->requestLayout();
      }
    }
  }
}

void KeybindingEditor::setFilter(std::string text) {
  if (text.size() > kMaxQueryBytes) text.resize(kMaxQueryBytes);
  if (text == filter_) return;
  filter_ = std::move(text);
  applyFilters();
}

void KeybindingEditor::setContextFilter(std::string context) {
  if (context == contextFilter_) return;
  contextFilter_ = std::move(context);
  applyFilters();
}

void KeybindingEditor::setTwoStepCapture(bool on) {
  if (on == twoStep_) return;
  twoStep_ = on;
  if (capture_.active) capture_.first.reset();
  if (Checkbox* box = ui().objectAs<Checkbox>(twoStepBox_)) box->setChecked(on);
}

void KeybindingEditor::setOnImport(std::function<void()> callback) {
  onImport_ = std::move(callback);
  if (WidgetObject* b = ui().object(import_)) b->setEnabled(static_cast<bool>(onImport_));
}

void KeybindingEditor::setOnExport(std::function<void()> callback) {
  onExport_ = std::move(callback);
  if (WidgetObject* b = ui().object(export_)) b->setEnabled(static_cast<bool>(onExport_));
}

// ---- queries ------------------------------------------------------------------------------------

KeybindingEditor::Row* KeybindingEditor::find(std::string_view commandId) {
  for (Row& row : rows_) {
    if (row.commandId == commandId) return &row;
  }
  return nullptr;
}

const KeybindingEditor::Row* KeybindingEditor::find(std::string_view commandId) const {
  for (const Row& row : rows_) {
    if (row.commandId == commandId) return &row;
  }
  return nullptr;
}

size_t KeybindingEditor::visibleRowCount() const {
  return static_cast<size_t>(std::count_if(rows_.begin(), rows_.end(), [](const Row& r) { return r.visible; }));
}

size_t KeybindingEditor::visibleHeadingCount() const {
  return static_cast<size_t>(std::count_if(headings_.begin(), headings_.end(), [](const Heading& h) { return h.visible; }));
}

bool KeybindingEditor::rowVisible(std::string_view commandId) const {
  const Row* row = find(commandId);
  return row != nullptr && row->visible;
}

WidgetId KeybindingEditor::rowOf(std::string_view commandId) const {
  const Row* row = find(commandId);
  return row != nullptr ? row->row : WidgetId{};
}

WidgetId KeybindingEditor::boxOf(std::string_view commandId, int slot) const {
  const Row* row = find(commandId);
  return row != nullptr && slot >= 0 && slot < cmd::kSlotCount ? row->boxes[slot] : WidgetId{};
}

ChordBox* KeybindingEditor::box(std::string_view commandId, int slot) { return ui().objectAs<ChordBox>(boxOf(commandId, slot)); }

WidgetId KeybindingEditor::resetButtonOf(std::string_view commandId) const {
  const Row* row = find(commandId);
  return row != nullptr ? row->reset : WidgetId{};
}

// ---- actions ------------------------------------------------------------------------------------

bool KeybindingEditor::resetCommand(std::string_view commandId) {
  if (capture_.active && capture_.commandId == commandId) cancelCapture();
  return services_.overrides.resetCommand(commandId);
}

bool KeybindingEditor::resetAllDialogOpen() { return resetDialog_.valid() && isDialogOpen(ui(), resetDialog_); }

void KeybindingEditor::requestResetAll() {
  if (resetAllDialogOpen()) return;
  DialogSpec spec;
  spec.title = "Reset all shortcuts";
  spec.description = "Every keyboard shortcut returns to its default. Your own shortcuts are removed.";
  spec.actions = {{"cancel", "Cancel", DialogActionKind::Neutral, true, true, true}, {"reset", "Reset all", DialogActionKind::Danger, false, false, true}};
  spec.owner = id();
  spec.maxWidth = 380.0;
  spec.onResult = [context = &ui(), self = id()](const DialogResult& result) {
    KeybindingEditor* e = context->objectAs<KeybindingEditor>(self);
    if (e == nullptr) return;
    e->resetDialog_ = {};
    if (result.action != "reset") return;
    e->cancelCapture();
    e->services_.overrides.resetAll();
  };
  resetDialog_ = openDialog(ui(), std::move(spec));
}

void KeybindingEditor::boxNavigate(ChordBox& from, int rows) {
  size_t index = rows_.size();
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].boxes[from.slot()] == from.id()) index = i;
  }
  if (index == rows_.size() || rows == 0) return;
  for (size_t i = index;;) {
    if (rows < 0) {
      if (i == 0) return;
      --i;
    } else {
      if (i + 1 >= rows_.size()) return;
      ++i;
    }
    if (!rows_[i].visible) continue;
    ui().focusWidget(rows_[i].boxes[from.slot()], core::events::FocusReason::Keyboard);
    if (ScrollArea* scroll = ui().objectAs<ScrollArea>(scroll_)) scroll->scrollIntoView(rows_[i].row);
    return;
  }
}

}  // namespace r1ui::widgets
