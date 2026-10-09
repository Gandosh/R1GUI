// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PropertyPanel.h: the search row, the generated sections and rows, the
//   coalesced refresh/rebuild machinery and the textual description used by golden tests.
// Why: see PropertyPanel.h. The panel never edits values itself; it only builds views and keeps them in
//   step with the PropertyContext and PanelState it was given.
// Invariants: at most one zero-delay timer is pending (cancelled on detach, nothing armed when idle);
//   the cached model equals the model the current widgets were built from (up to modified flags and
//   the collapse toggles the user made, which the panel mirrors); a rebuild never runs while an
//   interaction is open unless the selection itself changed.
// Callers: hosts, gallery, tests.
#include "r1ui/widgets/props/PropertyPanel.h"

#include <algorithm>

#include "r1ui/widgets/iconbutton/IconButton.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/props/PropControls.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/scroll/ScrollArea.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace r1ui::widgets {

namespace {

namespace layout = core::layout;
using core::tree::WidgetId;

constexpr double kSideInset = 12.0;  // docs/spec/widgets.md 3: panel padding

void setDisplayed(UiContext& ui, WidgetId id, bool displayed) {
  if (core::tree::Widget* w = ui.tree().get(id)) {
    const layout::Display wanted = displayed ? layout::Display::Flex : layout::Display::None;
    if (w->style.display == wanted) return;
    w->style.display = wanted;
    ui.invalidator().requestLayout(id);
  }
}

// Two models describe the same widgets when everything the widgets are built from matches; the
// "modified" flag only changes the enablement of the reset action, which refresh updates in place.
bool sameStructure(const props::PanelModel& a, const props::PanelModel& b) {
  if (a.searchVisible != b.searchVisible || a.categories.size() != b.categories.size()) return false;
  for (size_t i = 0; i < a.categories.size(); ++i) {
    const props::PanelCategory& x = a.categories[i];
    const props::PanelCategory& y = b.categories[i];
    if (x.name != y.name || x.collapsed != y.collapsed || x.rows != y.rows) return false;
  }
  return true;
}

}  // namespace

int64_t UiClock::nowMs() const { return static_cast<int64_t>(ui_->now()); }

// ---- one category ----------------------------------------------------------------------------------------------

CategoryWidgets buildPropertyCategory(UiContext& ui, WidgetId parent, props::PropertyContext& context, const props::PanelCategory& category,
                                      const PropertyPanelOptions& options, std::function<void(bool)> onCollapsed, std::vector<std::pair<size_t, WidgetId>>* rowsOut) {
  PropertySection& section = ui.create<PropertySection>(parent, category.name.empty() ? options.defaultCategory : category.name, SectionOptions{.collapsible = true});
  ActionButton& reset = section.addAction("undo2", options.resetCategoryTooltip, [&context, name = category.name](ActionButton&) { context.resetCategory(std::string_view(name)); });
  reset.setEnabled(category.anyModified);

  std::string group;
  for (const props::PanelRow& r : category.rows) {
    const std::string& rowGroup = context.descriptor(r.row).group;
    if (rowGroup != group && !rowGroup.empty()) {
      Label& caption = ui.create<Label>(section.content(), rowGroup, LabelRole::Muted);
      caption.style().margin[layout::kTop] = layout::Length::px(4);
    }
    group = rowGroup;
    PropertyRowView& view = ui.create<PropertyRowView>(section.content(), context, r.row, options.row);
    if (rowsOut != nullptr) rowsOut->emplace_back(r.row, view.id());
  }
  section.setCollapsed(category.collapsed);
  section.setOnToggle([onCollapsed = std::move(onCollapsed)](PropertySection& s) {
    if (onCollapsed) onCollapsed(s.collapsed());
  });
  return {section.id(), reset.id()};
}

// ---- panel --------------------------------------------------------------------------------------------------------

PropertyPanel::PropertyPanel(props::PropertyContext& context, props::PanelState& state, PropertyPanelOptions options)
    : ctx_(&context), state_(&state), options_(std::move(options)) {}

PropertyPanel::~PropertyPanel() = default;

void PropertyPanel::onAttached() {
  layout::Style& s = style();
  s.direction = layout::FlexDirection::Column;
  s.alignItems = layout::Align::Stretch;
  s.flexGrow = 1.0;
  s.flexShrink = 1.0;
  s.minWidth = layout::Length::px(0);
  s.minHeight = layout::Length::px(0);

  SectionBox& row = ui().create<SectionBox>(id());
  row.style().direction = layout::FlexDirection::Row;
  row.style().alignItems = layout::Align::Center;
  row.style().gapColumn = 6.0;
  row.style().padding[layout::kLeft] = kSideInset;
  row.style().padding[layout::kRight] = kSideInset;
  row.style().padding[layout::kTop] = 8.0;
  row.style().padding[layout::kBottom] = 8.0;
  row.style().flexShrink = 0.0;
  searchRow_ = row.id();

  TextInput& search = ui().create<TextInput>(searchRow_, TextInputTone::Panel, TextInputSize::Md);
  search.style().flexGrow = 1.0;
  search.style().flexShrink = 1.0;
  search.style().flexBasis = layout::Length::px(0);
  search.style().minWidth = layout::Length::px(0);
  search.setPlaceholder(options_.searchPlaceholder);
  search.setClearable(true);
  search.setText(state_->search());
  search.setOnTextChanged([this](std::string_view text) { setSearchText(std::string(text)); });
  search_ = search.id();

  IconButton& advanced = ui().create<IconButton>(searchRow_, "sliders-horizontal", IconButtonSize::Md);
  advanced.setTooltip(options_.advancedTooltip);
  advanced.setActive(state_->showAdvanced());
  advanced.setOnClick([this] {
    state_->setShowAdvanced(!state_->showAdvanced());
    if (IconButton* b = advancedButton()) b->setActive(state_->showAdvanced());
    request(Pending::Refresh);
    flush();
  });
  advanced_ = advanced.id();

  ScrollArea& scroll = ui().create<ScrollArea>(id());
  scroll.style().flexGrow = 1.0;
  scroll.style().flexShrink = 1.0;
  scroll.style().flexBasis = layout::Length::px(0);
  scroll.style().minHeight = layout::Length::px(0);
  scroll_ = scroll.id();

  token_ = ctx_->notifier().subscribe([this](const props::ChangeEvent& e) { request(e.kind == props::ChangeKind::Selection ? Pending::Rebuild : Pending::Refresh); });
  model_ = props::buildPanelModel(*ctx_, *state_);
  rebuild();
  built_ = true;
}

void PropertyPanel::onDetached() {
  if (timer_ != 0) ui().cancelTimer(timer_);
  timer_ = 0;
  if (token_ != 0) ctx_->notifier().unsubscribe(token_);
  token_ = 0;
  built_ = false;
}

// ---- coalescing -----------------------------------------------------------------------------------------------------

// Any number of notices before the next tick cost one flush (spec 09 rule 6).
void PropertyPanel::request(Pending level) {
  if (!built_) return;
  if (level > pending_) pending_ = level;
  if (timer_ != 0) return;
  const WidgetId self = id();
  UiContext* u = &ui();
  timer_ = u->setTimer(0, [u, self] {
    if (auto* panel = u->objectAs<PropertyPanel>(self)) {
      panel->timer_ = 0;
      panel->flush();
    }
  });
  if (timer_ == 0) flush();  // the timer table is full: do it now rather than never
}

void PropertyPanel::flush() {
  if (!built_) return;
  const Pending level = pending_;
  pending_ = Pending::None;
  if (timer_ != 0) {
    ui().cancelTimer(timer_);
    timer_ = 0;
  }
  if (level == Pending::None) return;
  props::PanelModel next = props::buildPanelModel(*ctx_, *state_);
  const bool changed = level == Pending::Rebuild || !sameStructure(next, model_);
  // A structural change waits for the end of a gesture, except a new selection (the rows are gone).
  if (changed && (level == Pending::Rebuild || !ctx_->interacting())) {
    model_ = std::move(next);
    rebuild();
    return;
  }
  refreshRows();
  // The reset actions follow the modified flags of the fresh model without touching the structure.
  for (size_t i = 0; i < resets_.size() && i < next.categories.size() && i < model_.categories.size(); ++i) {
    if (next.categories[i].name == model_.categories[i].name) {
      model_.categories[i].anyModified = next.categories[i].anyModified;
      if (WidgetObject* b = ui().object(resets_[i])) b->setEnabled(next.categories[i].anyModified);
    }
  }
}

void PropertyPanel::refreshRows() {
  for (const auto& [row, view] : rows_) {
    if (auto* v = ui().objectAs<PropertyRowView>(view)) v->refresh();
  }
}

void PropertyPanel::rebuild() {
  for (const WidgetId section : sections_) ui().destroy(section);
  if (notice_.valid()) ui().destroy(notice_);
  sections_.clear();
  resets_.clear();
  rows_.clear();
  notice_ = {};

  setDisplayed(ui(), searchRow_, model_.searchVisible);
  if (TextInput* box = searchBox()) {
    if (box->text() != state_->search() && !box->focused()) box->setText(state_->search());
  }
  if (IconButton* b = advancedButton()) b->setActive(state_->showAdvanced());

  ScrollArea* scroll = ui().objectAs<ScrollArea>(scroll_);
  if (scroll == nullptr) return;
  if (model_.categories.empty()) {
    Label& notice = ui().create<Label>(scroll->content(), ctx_->rowCount() == 0 ? options_.emptyText : options_.noMatchText, LabelRole::Muted);
    notice.style().margin[layout::kLeft] = layout::Length::px(kSideInset);
    notice.style().margin[layout::kTop] = layout::Length::px(kSideInset);
    notice_ = notice.id();
    return;
  }
  for (const props::PanelCategory& category : model_.categories) {
    const CategoryWidgets parts = buildPropertyCategory(
        ui(), scroll->content(), *ctx_, category, options_,
        [this, name = category.name](bool collapsed) {
          if (!state_->setCategoryCollapsed(name, collapsed)) {
            if (PropertySection* s = sectionNamed(name)) s->setCollapsed(false);  // a search keeps every category open
            return;
          }
          for (props::PanelCategory& c : model_.categories) {
            if (c.name == name) c.collapsed = collapsed;  // keep the cached model equal to what is on screen
          }
        },
        &rows_);
    sections_.push_back(parts.section);
    resets_.push_back(parts.resetAction);
  }
}

// ---- search ---------------------------------------------------------------------------------------------------------

void PropertyPanel::setSearchText(std::string text) {
  state_->setSearch(text);
  if (TextInput* box = searchBox()) {
    if (box->text() != text) box->setText(text);
  }
  request(Pending::Refresh);
  flush();
}

TextInput* PropertyPanel::searchBox() const { return ui().objectAs<TextInput>(search_); }
IconButton* PropertyPanel::advancedButton() const { return ui().objectAs<IconButton>(advanced_); }
ScrollArea* PropertyPanel::scrollArea() const { return ui().objectAs<ScrollArea>(scroll_); }

PropertySection* PropertyPanel::section(size_t index) const {
  return index < sections_.size() ? ui().objectAs<PropertySection>(sections_[index]) : nullptr;
}

PropertySection* PropertyPanel::sectionNamed(const std::string& category) const {
  for (size_t i = 0; i < model_.categories.size() && i < sections_.size(); ++i) {
    if (model_.categories[i].name == category) return section(i);
  }
  return nullptr;
}

PropertyRowView* PropertyPanel::rowView(size_t contextRow) const {
  for (const auto& [row, view] : rows_) {
    if (row == contextRow) return ui().objectAs<PropertyRowView>(view);
  }
  return nullptr;
}

std::string PropertyPanel::describe() const {
  std::string out = "panel search=\"" + state_->search() + "\" advanced=" + (state_->showAdvanced() ? "1" : "0") + " searchbox=" + (model_.searchVisible ? "shown" : "hidden") + "\n";
  if (notice_.valid()) {
    if (const auto* label = ui().objectAs<Label>(notice_)) out += "notice \"" + label->text() + "\"\n";
  }
  for (size_t i = 0; i < model_.categories.size() && i < sections_.size(); ++i) {
    const PropertySection* s = section(i);
    if (s == nullptr) continue;
    const WidgetObject* reset = ui().object(resets_[i]);
    out += "section \"" + s->title() + "\" " + (s->collapsed() ? "collapsed" : "open") + " reset=" + (reset != nullptr && reset->enabled() ? "enabled" : "disabled") + "\n";
    for (const props::PanelRow& r : model_.categories[i].rows) {
      if (const PropertyRowView* v = rowView(r.row)) out += "  " + v->describe() + "\n";
    }
  }
  return out;
}

}  // namespace r1ui::widgets
