// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ActionList (ActionList.h): the search field above the view, the live
//   binding to a CommandRegistry and the type-to-search hand-over from the list.
// Invariants: the registry subscription is removed in onDetached; the view and the search field are
//   created once in onAttached; a typed character is appended to the search text only when it is a
//   valid scalar value; the query shown in the field and the view's query are set together.
// Callers: HotkeyEditor, hosts, the gallery, tests.
#include "r1ui/widgets/actions/ActionList.h"

#include <algorithm>

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace r1ui::widgets {

namespace layout = core::layout;
using core::events::Key;

namespace {

// Appends the UTF-8 encoding of a scalar value; false for surrogates and values above U+10FFFF.
bool appendUtf8(std::string& out, char32_t cp) {
  if (cp >= 0xD800 && cp <= 0xDFFF) return false;
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0x10FFFF) {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    return false;
  }
  return true;
}

}  // namespace

void ActionList::onAttached() {
  style().direction = layout::FlexDirection::Column;
  style().gapRow = 6.0;
  style().minHeight = layout::Length::px(0);
  setWantsLayoutCallback(true);
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  if (options_.showSearch) {
    TextInput& field = ui().create<TextInput>(id(), TextInputTone::Default, TextInputSize::Md);
    field.setPlaceholder(options_.searchPlaceholder);
    field.setClearable(true);
    field.setAccessibleName("Search actions");
    field.style().flexShrink = 0.0;
    search_ = field.id();
    field.setOnTextChanged([context, self](std::string_view text) {
      if (ActionList* list = context->objectAs<ActionList>(self)) list->view().setQuery(text);
    });
    field.setOnCommitted([context, self](std::string_view) {
      if (ActionList* list = context->objectAs<ActionList>(self)) list->view().activateSelected();
    });
  }
  ActionListView& view = ui().create<ActionListView>(id(), options_);
  view_ = view.id();
  view.setOnSearchRequest([context, self](char32_t cp) {
    if (ActionList* list = context->objectAs<ActionList>(self)) list->searchRequested(cp);
  });
}

void ActionList::onDetached() {
  if (registry_ != nullptr) registry_->unsubscribe(listener_);
  registry_ = nullptr;
  keymap_ = nullptr;
}

ActionListView& ActionList::view() const { return *ui().objectAs<ActionListView>(view_); }

void ActionList::bindRegistry(commands::CommandRegistry* registry, const commands::Keymap* keymap, ActionSourceOptions source) {
  if (registry_ != nullptr) registry_->unsubscribe(listener_);
  listener_ = 0;
  registry_ = registry;
  keymap_ = keymap;
  source_ = source;
  if (registry_ == nullptr || keymap_ == nullptr) {
    registry_ = nullptr;
    keymap_ = nullptr;
    return;
  }
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  listener_ = registry_->subscribe([context, self] {
    ActionList* list = context->objectAs<ActionList>(self);
    if (list == nullptr) return;
    list->dirty_ = true;  // coalesced: applied by the next layout pass or flush()
    list->requestLayout();
  });
  refreshFromRegistry();
}

void ActionList::flush() {
  if (dirty_) refreshFromRegistry();
}

void ActionList::refreshFromRegistry() {
  dirty_ = false;
  if (registry_ == nullptr || keymap_ == nullptr) return;
  view().setActions(actionsFromRegistry(*registry_, *keymap_, source_));
}

void ActionList::setFilter(const std::string& text) {
  if (TextInput* field = ui().objectAs<TextInput>(search_)) {
    if (field->text() != text) field->setText(text);
  }
  view().setQuery(text);
}

void ActionList::focusSearch() {
  if (!search_.valid()) return;
  ui().focusWidget(search_, core::events::FocusReason::Keyboard);
  if (TextInput* field = ui().objectAs<TextInput>(search_)) field->selectAll();
}

// Type-to-search: the character that reached the list starts (or extends) the search text and the
// keyboard moves to the field; 0 (Ctrl+F) only moves the focus.
void ActionList::searchRequested(char32_t codePoint) {
  TextInput* field = ui().objectAs<TextInput>(search_);
  if (field == nullptr) return;
  if (codePoint == 0) {
    focusSearch();
    return;
  }
  std::string text = field->text();
  if (!appendUtf8(text, codePoint)) return;
  field->setText(text);
  ui().focusWidget(search_, core::events::FocusReason::Keyboard);
  field->setSelection(field->text().size(), field->text().size());
  view().setQuery(field->text());
}

void ActionList::onKeyDown(Event& e) {
  ActionListView& v = view();
  const int page = std::max(1, static_cast<int>(v.viewportHeight() / ActionListView::kRowHeight) - 1);
  switch (e.key) {
    case Key::Down: v.moveSelection(1); break;
    case Key::Up: v.moveSelection(-1); break;
    case Key::PageDown: v.moveSelection(page); break;
    case Key::PageUp: v.moveSelection(-page); break;
    default:
      if (e.key == static_cast<Key>('F') && (e.modifiers & core::events::Mod::kCtrl) != 0) {
        focusSearch();
        e.markHandled();
      }
      return;
  }
  e.markHandled();
}

}  // namespace r1ui::widgets
