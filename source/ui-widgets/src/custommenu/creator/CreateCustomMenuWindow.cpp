// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CreateCustomMenuWindow.h: the type chooser page, the editor page (left target,
//   right action list, footer), the controls' wiring to the draft, and the rebuild when the session's draft
//   changes.
// Invariants: every control that mirrors the draft is refreshed from it in refreshControls() under the
//   refreshing_ guard (so programmatic updates never come back as user edits); the content is rebuilt only
//   from a timer or flush(), never inside the handler that asked for it; the drag hub is cancelled before
//   any widget it may point at is destroyed; after a hook runs nothing touches members (a hook may
//   destroy the window).
// Callers: the host's dock, the gallery, tests.
#include "r1ui/widgets/custommenu/creator/CreateCustomMenuWindow.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

#include "r1ui/widgets/actions/ActionList.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/button/Pressable.h"
#include "r1ui/widgets/checkbox/Checkbox.h"
#include "r1ui/widgets/custommenu/creator/PanelPreviewEditor.h"
#include "r1ui/widgets/custommenu/creator/PiePreviewEditor.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/section/Section.h"
#include "r1ui/widgets/segmented/Segmented.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace r1ui::widgets {

namespace cm = commands::custommenu;
namespace layout = core::layout;
using core::tree::WidgetId;
using theme::State::kNone;
using theme::State::kHover;
using theme::StyleProperty;

namespace {

constexpr theme::StyleRuleEntry kCardRows[] = {
    {"creator.card", kNone, StyleProperty::Background, "color:panel-field"},
    {"creator.card", kNone, StyleProperty::BorderColor, "color:border"},
    {"creator.card", kNone, StyleProperty::BorderWidth, "number:1"},
    {"creator.card", kNone, StyleProperty::Radius, "number:10"},
    {"creator.card", kHover, StyleProperty::Background, "color:panel-field-hover"},
    {"creator.card", kHover, StyleProperty::BorderColor, "color:accent"},
};

constexpr int kButtonSizes[] = {28, 40, 56, 72};
constexpr const char* kButtonSizeNames[] = {"Small", "Medium", "Large", "Extra large"};
constexpr int kMaxColumnChoices = 6;

// A big pressable card of the type chooser: icon, title and a few lines of explanation.
class TypeCard final : public Pressable {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows() { return kCardRows; }
  TypeCard(std::string icon, std::string title, std::vector<std::string> lines, std::function<void()> pick)
      : icon_(std::move(icon)), title_(std::move(title)), lines_(std::move(lines)), pick_(std::move(pick)) {}
  const char* typeName() const override { return "CreatorTypeCard"; }
  std::string_view accessibleName() const override { return title_; }
  void onAttached() override {
    Pressable::onAttached();
    style().width = layout::Length::px(300.0);
    style().height = layout::Length::px(200.0);
    style().flexShrink = 0.0;
  }
  void paint(PaintContext& ctx) override {
    const theme::ResolvedStyle& rs = ctx.style("creator.card");
    const render::Rect box = ctx.box();
    const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(rs.radius));
    ctx.painter().fillRoundedRect(box, radii, ctx.color(rs.background));
    ctx.painter().border(box, radii, ctx.px(hovered() ? 2.0 : rs.border.width), ctx.color(rs.border.color));
    const layout::Rect self = ctx.rect();
    ctx.drawIcon(icon_, 32.0, ctx.toPhysical(self.x + 20.0, self.y + 20.0, 32.0, 32.0), ctx.color("accent"), "circle");
    TextOptions title;
    title.weight = 600;
    ctx.drawText(title_, ctx.style("label.body").text, ctx.toPhysical(self.x + 20.0, self.y + 64.0, self.w - 40.0, 24.0), title);
    const theme::TextStyle muted = ctx.style("label.muted").text;
    double y = self.y + 96.0;
    for (const std::string& line : lines_) {
      ctx.drawText(line, muted, ctx.toPhysical(self.x + 20.0, y, self.w - 40.0, 20.0), {});
      y += 20.0;
    }
  }
  void paintOver(PaintContext& ctx) override {
    if (focusVisible()) ctx.focusRing(ctx.px(10.0));
  }

 protected:
  void activate() override {
    const auto callback = pick_;  // the handler rebuilds the window around this card
    if (callback) callback();
  }

 private:
  std::string icon_, title_;
  std::vector<std::string> lines_;
  std::function<void()> pick_;
};

SectionBox& box(UiContext& ui, WidgetId parent, layout::FlexDirection direction, double gap) {
  SectionBox& b = ui.create<SectionBox>(parent);
  b.style().direction = direction;
  b.style().gapRow = gap;
  b.style().gapColumn = gap;
  b.style().flexShrink = 0.0;
  return b;
}

void grow(layout::Style& s) {
  s.flexGrow = 1.0;
  s.flexShrink = 1.0;
  s.minWidth = layout::Length::px(0.0);
  s.minHeight = layout::Length::px(0.0);
}

Segmented& segmented(UiContext& ui, WidgetId parent, const std::vector<std::string>& names) {
  Segmented& s = ui.create<Segmented>(parent, SegmentedSize::Sm);
  std::vector<SegmentItem> items;
  for (const std::string& n : names) items.push_back({n, {}, {}, true});
  s.setItems(std::move(items));
  return s;
}

}  // namespace

CreateCustomMenuWindow::CreateCustomMenuWindow(CommandServices services, CreatorSession& session, CreatorHooks hooks)
    : services_(services), session_(session), hooks_(std::move(hooks)) {}

// ---- lifecycle --------------------------------------------------------------------------------------

void CreateCustomMenuWindow::onAttached() {
  style().direction = layout::FlexDirection::Column;
  style().width = layout::Length::percent(100.0);
  style().height = layout::Length::percent(100.0);
  for (double& p : style().padding) p = 16.0;
  style().gapRow = 12.0;
  hub_ = std::make_unique<DragHub>(ui());
  session_.ensure();
  listener_ = session_.subscribe([this] { scheduleRebuild(); });
  rebuild();
}

void CreateCustomMenuWindow::onDetached() {
  session_.unsubscribe(listener_);
  listener_ = 0;
  if (hub_) hub_->cancel();
  // The hub is destroyed with the widget; the editors unregister themselves in their own onDetached,
  // which runs first (children are detached before their parent).
}

void CreateCustomMenuWindow::scheduleRebuild() {
  if (rebuildPending_) return;
  rebuildPending_ = true;
  ui().setTimer(0, [context = &ui(), self = id()] {
    if (!context->alive(self)) return;
    if (CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self)) {
      w->rebuildPending_ = false;
      w->rebuild();
    }
  });
}

void CreateCustomMenuWindow::flush() {
  if (builtGeneration_ != session_.generation()) rebuild();
}

void CreateCustomMenuWindow::rebuild() {
  if (hub_) hub_->cancel();
  ui().overlays().closeAll();  // a context menu or dialog of the old content must not outlive it
  if (ui().alive(content_)) ui().destroy(content_);
  content_ = actions_ = pie_ = panel_ = nameField_ = labelField_ = createButton_ = cancelButton_ = saveFileButton_ = loadFileButton_ = {};
  clearButton_ = slotsSelector_ = columnsSelector_ = sizeSelector_ = labelsCheckbox_ = typeSelector_ = pieCard_ = panelCard_ = {};
  statusLabel_ = issueLabel_ = selectionLabel_ = titleLabel_ = {};
  selected_ = -1;
  status_.clear();
  if (!session_.active()) session_.ensure();
  builtGeneration_ = session_.generation();
  SectionBox& content = box(ui(), id(), layout::FlexDirection::Column, 12.0);
  grow(content.style());
  content_ = content.id();
  if (session_.typeChosen()) buildEditor(content_);
  else buildChooser(content_);
}

// ---- the type chooser -------------------------------------------------------------------------------

void CreateCustomMenuWindow::chooseType(cm::MenuKind kind) { session_.chooseType(kind); }

void CreateCustomMenuWindow::buildChooser(WidgetId parent) {
  ui().create<Label>(parent, "Create Custom Menu", LabelRole::Title);
  ui().create<Label>(parent, "What kind of menu do you want to make?", LabelRole::Muted);
  SectionBox& cards = box(ui(), parent, layout::FlexDirection::Row, 20.0);
  cards.style().margin[layout::kTop] = layout::Length::px(12.0);
  cards.style().wrap = layout::FlexWrap::Wrap;
  const WidgetId self = id();
  UiContext* context = &ui();
  const auto pick = [context, self](cm::MenuKind kind) {
    return [context, self, kind] {
      if (CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self)) w->chooseType(kind);
    };
  };
  panelCard_ = ui().create<TypeCard>(cards.id(), "layout-panel-top", "Dockable panel",
                                     std::vector<std::string>{"A panel of action buttons that docks", "like your other panels. It is listed", "under Custom Menus, so you can", "close it and open it again."},
                                     pick(cm::MenuKind::Panel)).id();
  pieCard_ = ui().create<TypeCard>(cards.id(), "mouse-pointer-click", "Pie menu",
                                   std::vector<std::string>{"Up to 8 actions around the pointer.", "Hold the right mouse button in the", "viewport and flick toward a slot", "to run its action."},
                                   pick(cm::MenuKind::Pie)).id();
  SectionBox& spacer = box(ui(), parent, layout::FlexDirection::Row, 0.0);
  grow(spacer.style());
  SectionBox& bar = box(ui(), parent, layout::FlexDirection::Row, 8.0);
  bar.style().justifyContent = layout::Justify::End;
  if (hooks_.loadFile) {
    Button& load = ui().create<Button>(bar.id(), "Load from file...", ButtonTone::Panel, ButtonSize::Md);
    load.setIcon("folder-open");
    load.setOnClick([context, self] {
      if (CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self)) {
        auto callback = w->hooks_.loadFile;
        if (callback) callback();
      }
    });
    loadFileButton_ = load.id();
  }
  Button& cancel = ui().create<Button>(bar.id(), "Cancel", ButtonTone::Panel, ButtonSize::Md);
  cancel.setOnClick([context, self] {
    if (CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self)) w->cancel();
  });
  cancelButton_ = cancel.id();
}

// ---- the editor -------------------------------------------------------------------------------------

void CreateCustomMenuWindow::buildEditor(WidgetId parent) {
  cm::MenuDraft* d = draft();
  if (d == nullptr) return;
  const WidgetId self = id();
  UiContext* context = &ui();

  SectionBox& header = box(ui(), parent, layout::FlexDirection::Row, 12.0);
  header.style().alignItems = layout::Align::Center;
  Label& title = ui().create<Label>(header.id(), d->editing() ? "Edit Custom Menu" : "Create Custom Menu", LabelRole::Title);
  title.style().flexGrow = 1.0;
  titleLabel_ = title.id();
  ui().create<Label>(header.id(), "Type", LabelRole::Muted);
  Segmented& type = segmented(ui(), header.id(), {"Pie menu", "Dockable panel"});
  type.setSelectedIndex(d->kind() == cm::MenuKind::Pie ? 0 : 1);
  type.setEnabled(!d->editing());
  type.setOnChange([context, self](int index) {
    if (CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self)) w->chooseType(index == 0 ? cm::MenuKind::Pie : cm::MenuKind::Panel);
  });
  typeSelector_ = type.id();

  SectionBox& main = box(ui(), parent, layout::FlexDirection::Row, 16.0);
  grow(main.style());
  main.style().alignItems = layout::Align::Stretch;
  SectionBox& left = box(ui(), main.id(), layout::FlexDirection::Column, 10.0);
  grow(left.style());
  SectionBox& right = box(ui(), main.id(), layout::FlexDirection::Column, 8.0);
  right.style().width = layout::Length::px(440.0);
  right.style().minHeight = layout::Length::px(0.0);
  buildLeft(left.id());
  buildRight(right.id());
  buildFooter(parent);
  refreshControls();
}

void CreateCustomMenuWindow::buildLeft(WidgetId parent) {
  cm::MenuDraft* d = draft();
  if (d == nullptr) return;
  const bool pie = d->kind() == cm::MenuKind::Pie;
  const WidgetId self = id();
  UiContext* context = &ui();
  ui().create<Label>(parent, pie ? "Drag an action onto a slot. Drag a slot onto another to swap. Right-click clears." : "Drag actions into the panel. Drag a button to move it. Right-click removes.",
                     LabelRole::Muted);

  const auto wire = [this](auto& editor) {
    editor.setDragHub(hub_.get());
    editor.setOnChanged([this] { draftChanged(); });
    editor.setOnSelect([this](int index) { selectionChanged(index); });
    editor.setOnMessage([this](const std::string& text) { say(text); });
  };
  if (pie) {
    SectionBox& row = box(ui(), parent, layout::FlexDirection::Row, 0.0);
    row.style().justifyContent = layout::Justify::Center;
    PiePreviewEditor& editor = ui().create<PiePreviewEditor>(row.id(), services_, session_);
    wire(editor);
    pie_ = editor.id();
    SectionBox& settings = box(ui(), parent, layout::FlexDirection::Row, 8.0);
    settings.style().alignItems = layout::Align::Center;
    settings.style().justifyContent = layout::Justify::Center;
    ui().create<Label>(settings.id(), "Slots", LabelRole::Muted);
    Segmented& slots = segmented(ui(), settings.id(), {"4", "6", "8"});
    slots.setOnChange([context, self](int index) {
      CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self);
      if (w == nullptr || w->refreshing_ || w->draft() == nullptr) return;
      const cm::MenuEditResult result = w->draft()->setPieSlotCount(index == 0 ? 4 : (index == 1 ? 6 : 8));
      if (!result.ok) w->say(result.reason.empty() ? "The number of slots was not changed." : result.reason);
      if (PiePreviewEditor* editor = w->pieEditor()) editor->refresh();
      w->refreshControls();
    });
    slotsSelector_ = slots.id();
  } else {
    PanelPreviewEditor& editor = ui().create<PanelPreviewEditor>(parent, services_, session_);
    wire(editor);
    panel_ = editor.id();
    SectionBox& settings = box(ui(), parent, layout::FlexDirection::Row, 8.0);
    settings.style().alignItems = layout::Align::Center;
    settings.style().wrap = layout::FlexWrap::Wrap;
    ui().create<Label>(settings.id(), "Columns", LabelRole::Muted);
    std::vector<std::string> columnNames;
    for (int i = 1; i <= kMaxColumnChoices; ++i) columnNames.push_back(std::to_string(i));
    Segmented& columns = segmented(ui(), settings.id(), columnNames);
    columns.setOnChange([context, self](int index) {
      CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self);
      if (w == nullptr || w->refreshing_ || w->draft() == nullptr) return;
      w->draft()->setPanelColumns(index + 1);
      if (PanelPreviewEditor* editor = w->panelEditor()) editor->refresh();
      w->refreshControls();
    });
    columnsSelector_ = columns.id();
    ui().create<Label>(settings.id(), "Button size", LabelRole::Muted);
    Segmented& sizes = segmented(ui(), settings.id(), std::vector<std::string>(std::begin(kButtonSizeNames), std::end(kButtonSizeNames)));
    sizes.setOnChange([context, self](int index) {
      CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self);
      if (w == nullptr || w->refreshing_ || w->draft() == nullptr || index < 0 || index >= 4) return;
      w->draft()->setPanelButtonSize(kButtonSizes[index]);
      if (PanelPreviewEditor* editor = w->panelEditor()) editor->refresh();
      w->refreshControls();
    });
    sizeSelector_ = sizes.id();
    Checkbox& labels = ui().create<Checkbox>(settings.id(), "Show labels");
    labels.setOnChange([context, self](bool on) {
      CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self);
      if (w == nullptr || w->refreshing_ || w->draft() == nullptr) return;
      w->draft()->setPanelShowLabels(on);
      if (PanelPreviewEditor* editor = w->panelEditor()) editor->refresh();
      w->refreshControls();
    });
    labelsCheckbox_ = labels.id();
  }

  // The selected slot or button: what it is, an optional label of its own, and Clear/Remove.
  SectionBox& selection = box(ui(), parent, layout::FlexDirection::Row, 8.0);
  selection.style().alignItems = layout::Align::Center;
  Label& what = ui().create<Label>(selection.id(), "Nothing selected", LabelRole::Muted);
  what.style().flexGrow = 1.0;
  what.style().minWidth = layout::Length::px(0.0);
  selectionLabel_ = what.id();
  TextInput& label = ui().create<TextInput>(selection.id(), TextInputTone::Default, TextInputSize::Sm);
  label.style().width = layout::Length::px(190.0);
  label.setPlaceholder("Label (optional)");
  label.setMaxLength(48);
  label.setAccessibleName("Label of the selected entry");
  label.setOnTextChanged([context, self](std::string_view text) {
    CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self);
    if (w == nullptr || w->refreshing_ || w->draft() == nullptr || w->selected_ < 0) return;
    w->draft()->setLabel(static_cast<size_t>(w->selected_), std::string(text));
    if (PiePreviewEditor* editor = w->pieEditor()) editor->refresh();
    if (PanelPreviewEditor* editor = w->panelEditor()) editor->refresh();
  });
  labelField_ = label.id();
  Button& clear = ui().create<Button>(selection.id(), pie ? "Clear slot" : "Remove", ButtonTone::Panel, ButtonSize::Sm);
  clear.setOnClick([context, self] {
    CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self);
    if (w == nullptr) return;
    if (PiePreviewEditor* editor = w->pieEditor()) editor->clearSlot(editor->selected());
    if (PanelPreviewEditor* editor = w->panelEditor()) editor->removeEntry(editor->selected());
  });
  clearButton_ = clear.id();
}

void CreateCustomMenuWindow::buildRight(WidgetId parent) {
  const WidgetId self = id();
  UiContext* context = &ui();
  ui().create<Label>(parent, "Actions", LabelRole::Heading);
  ActionListOptions options;
  options.shortcutWidth = 96.0;
  ActionList& list = ui().create<ActionList>(parent, options);
  grow(list.style());
  list.bindRegistry(&services_.registry, &services_.keymap);
  list.setDragHub(hub_.get());
  list.view().setOnActivate([context, self](const ActionInfo& action) {
    if (CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self)) w->addAction(action.id);
  });
  actions_ = list.id();
  ui().create<Label>(parent, "Drag an action onto the preview, or double-click it to add it.", LabelRole::Muted);
}

void CreateCustomMenuWindow::buildFooter(WidgetId parent) {
  cm::MenuDraft* d = draft();
  if (d == nullptr) return;
  const WidgetId self = id();
  UiContext* context = &ui();
  Label& status = ui().create<Label>(parent, "", LabelRole::Muted);
  status.style().minHeight = layout::Length::px(18.0);
  statusLabel_ = status.id();

  SectionBox& row = box(ui(), parent, layout::FlexDirection::Row, 10.0);
  row.style().alignItems = layout::Align::Center;
  ui().create<Label>(row.id(), "Name", LabelRole::Body);
  TextInput& name = ui().create<TextInput>(row.id());
  name.style().width = layout::Length::px(260.0);
  name.setPlaceholder("Menu name");
  name.setMaxLength(64);
  name.setAccessibleName("Menu name");
  name.setText(d->name());
  name.setOnTextChanged([context, self](std::string_view text) {
    CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self);
    if (w == nullptr || w->refreshing_ || w->draft() == nullptr) return;
    w->draft()->setName(std::string(text));
    w->status_.clear();
    w->refreshControls();
  });
  nameField_ = name.id();
  Label& issue = ui().create<Label>(row.id(), "", LabelRole::Muted);
  issue.style().flexGrow = 1.0;
  issue.style().minWidth = layout::Length::px(0.0);
  issueLabel_ = issue.id();

  if (hooks_.saveFile) {
    Button& save = ui().create<Button>(row.id(), "Save to file...", ButtonTone::Panel, ButtonSize::Md);
    save.setIcon("save");
    save.setTooltip("Write this menu to a .r1mn file");
    save.setOnClick([context, self] {
      CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self);
      if (w == nullptr || w->draft() == nullptr) return;
      cm::CustomMenu menu = w->draft()->snapshot();
      if (menu.name.empty()) {
        w->say("Enter a name before saving the menu to a file.");
        return;
      }
      auto callback = w->hooks_.saveFile;
      if (callback) callback(menu);
    });
    saveFileButton_ = save.id();
  }
  if (hooks_.loadFile) {
    Button& load = ui().create<Button>(row.id(), "Load from file...", ButtonTone::Panel, ButtonSize::Md);
    load.setIcon("folder-open");
    load.setTooltip("Start from the menu in a .r1mn file");
    load.setEnabled(!d->editing());
    load.setOnClick([context, self] {
      if (CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self)) {
        auto callback = w->hooks_.loadFile;
        if (callback) callback();
      }
    });
    loadFileButton_ = load.id();
  }
  Button& cancel = ui().create<Button>(row.id(), "Cancel", ButtonTone::Panel, ButtonSize::Md);
  cancel.setOnClick([context, self] {
    if (CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self)) w->cancel();
  });
  cancelButton_ = cancel.id();
  Button& create = ui().create<Button>(row.id(), d->editing() ? "Save" : "Create", ButtonTone::Accent, ButtonSize::Md);
  create.setOnClick([context, self] {
    if (CreateCustomMenuWindow* w = context->objectAs<CreateCustomMenuWindow>(self)) w->create();
  });
  createButton_ = create.id();
}

// ---- state mirrored into the controls ---------------------------------------------------------------

std::string CreateCustomMenuWindow::issue() const {
  const cm::MenuDraft* d = draft();
  if (d == nullptr) return {};
  const std::vector<std::string> problems = d->issues(session_.live());
  return problems.empty() ? std::string() : problems.front();
}

void CreateCustomMenuWindow::say(const std::string& text) {
  status_ = text;
  if (Label* label = ui().objectAs<Label>(statusLabel_)) label->setText(status_);
}

void CreateCustomMenuWindow::draftChanged() { refreshControls(); }

void CreateCustomMenuWindow::selectionChanged(int index) {
  selected_ = index;
  refreshControls();
}

void CreateCustomMenuWindow::refreshControls() {
  const cm::MenuDraft* d = draft();
  if (d == nullptr || !session_.typeChosen()) return;
  refreshing_ = true;
  const cm::CustomMenu& menu = d->menu();
  const bool pie = menu.kind == cm::MenuKind::Pie;
  if (Label* label = ui().objectAs<Label>(statusLabel_)) {
    if (label->text() != status_) label->setText(status_);
  }
  const std::string problem = issue();
  if (Label* label = ui().objectAs<Label>(issueLabel_)) {
    label->setText(problem);
    label->setColorToken("warning-action");
  }
  if (WidgetObject* create = ui().object(createButton_)) create->setEnabled(problem.empty());
  if (TextInput* name = ui().objectAs<TextInput>(nameField_); name != nullptr && name->text() != d->name()) name->setText(d->name());
  if (Segmented* type = ui().objectAs<Segmented>(typeSelector_)) type->setSelectedIndex(pie ? 0 : 1);

  const int count = static_cast<int>(menu.entries.size());
  const bool hasSelection = selected_ >= 0 && selected_ < count;
  const bool filled = hasSelection && !menu.entries[static_cast<size_t>(selected_)].commandId.empty();
  if (Label* what = ui().objectAs<Label>(selectionLabel_)) {
    std::string text = "Nothing selected";
    if (hasSelection) {
      const cm::MenuEntry& entry = menu.entries[static_cast<size_t>(selected_)];
      const commands::CommandDef* def = services_.registry.find(entry.commandId);
      const std::string what2 = !filled ? std::string("empty") : (def != nullptr ? def->label : entry.commandId + " (not available)");
      text = (pie ? "Slot " : "Button ") + std::to_string(selected_ + 1) + ": " + what2;
    }
    what->setText(text);
  }
  if (TextInput* field = ui().objectAs<TextInput>(labelField_)) {
    field->setEnabled(filled);
    const std::string want = filled ? menu.entries[static_cast<size_t>(selected_)].label : std::string();
    if (field->text() != want) field->setText(want);
  }
  if (WidgetObject* clear = ui().object(clearButton_)) clear->setEnabled(filled);

  if (Segmented* slots = ui().objectAs<Segmented>(slotsSelector_)) slots->setSelectedIndex(menu.slotCount == 4 ? 0 : (menu.slotCount == 6 ? 1 : 2));
  if (Segmented* columns = ui().objectAs<Segmented>(columnsSelector_)) columns->setSelectedIndex(menu.panel.columns >= 1 && menu.panel.columns <= kMaxColumnChoices ? menu.panel.columns - 1 : -1);
  if (Segmented* sizes = ui().objectAs<Segmented>(sizeSelector_)) {
    int best = 0;
    for (int i = 1; i < 4; ++i) {
      if (std::abs(kButtonSizes[i] - menu.panel.buttonSize) < std::abs(kButtonSizes[best] - menu.panel.buttonSize)) best = i;
    }
    sizes->setSelectedIndex(best);
  }
  if (Checkbox* labels = ui().objectAs<Checkbox>(labelsCheckbox_)) labels->setChecked(menu.panel.showLabels);
  refreshing_ = false;
}

// ---- operations -------------------------------------------------------------------------------------

ActionList* CreateCustomMenuWindow::actions() const { return ui().objectAs<ActionList>(actions_); }
PiePreviewEditor* CreateCustomMenuWindow::pieEditor() const { return ui().objectAs<PiePreviewEditor>(pie_); }
PanelPreviewEditor* CreateCustomMenuWindow::panelEditor() const { return ui().objectAs<PanelPreviewEditor>(panel_); }

bool CreateCustomMenuWindow::addAction(const std::string& commandId) {
  cm::MenuDraft* d = draft();
  if (d == nullptr || !session_.typeChosen()) return false;
  if (PanelPreviewEditor* editor = panelEditor()) return editor->dropCommand(-1, commandId);
  PiePreviewEditor* editor = pieEditor();
  if (editor == nullptr) return false;
  const std::vector<cm::MenuEntry>& entries = d->menu().entries;
  int slot = -1;
  const int selected = editor->selected();
  if (selected >= 0 && selected < static_cast<int>(entries.size()) && entries[static_cast<size_t>(selected)].commandId.empty()) slot = selected;
  for (int i = 0; slot < 0 && i < static_cast<int>(entries.size()); ++i) {
    if (entries[static_cast<size_t>(i)].commandId.empty()) slot = i;
  }
  if (slot < 0) slot = selected;  // every slot is filled: replace the selected one
  if (slot < 0) {
    say("Every slot is filled. Select a slot to replace its action.");
    return false;
  }
  return editor->dropCommand(slot, commandId);
}

bool CreateCustomMenuWindow::create() {
  cm::MenuDraft* d = draft();
  if (d == nullptr) return false;
  const std::vector<std::string> problems = d->issues(session_.live());
  if (!problems.empty()) {
    say(problems.front());
    return false;
  }
  const bool edited = d->editing();
  const cm::MenuEditResult result = d->commit(session_.live());
  if (!result.ok) {
    say(result.reason.empty() ? "The menu could not be saved." : result.reason);
    return false;
  }
  const std::string menuId = result.id;
  const auto hook = hooks_.committed;
  session_.end();  // the window rebuilds from a timer; a host that closes it first simply wins
  if (hook) hook(menuId, edited);
  return true;
}

void CreateCustomMenuWindow::cancel() {
  const auto hook = hooks_.cancelled;
  session_.end();
  if (hook) hook();
}

}  // namespace r1ui::widgets
