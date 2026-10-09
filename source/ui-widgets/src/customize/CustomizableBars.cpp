// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CustomizableBars.h.
// Invariants: exactly one child exists at a time (the bound widget or the edit display); the child is
//   replaced only when the edit mode or (outside edit mode) the model version changed; the command
//   bindings are dropped before the toolbar they bound is destroyed; with the default size step and gap
//   no style of the Toolbar or its buttons is touched.
// Callers: hosts, the gallery, tests.
#include "r1ui/widgets/customize/CustomizableBars.h"

#include <algorithm>

#include "r1ui/widgets/customize/LayoutConvert.h"

namespace r1ui::widgets {

namespace cz = commands::customize;
namespace layout = core::layout;

namespace {

// The flexible gap a spacer item becomes inside the bound toolbar.
class ToolbarSpacer final : public WidgetObject {
 public:
  const char* typeName() const override { return "ToolbarSpacer"; }
  void onAttached() override {
    style().flexGrow = 1.0;
    style().flexShrink = 0.0;
    style().minWidth = layout::Length::px(8.0);
    style().minHeight = layout::Length::px(8.0);
    node().flags.hitTestTransparent = true;
  }
};

// Same as bindCommandMenuBar, plus the user labels of command entries.
void bindMenus(MenuBar& bar, const CommandServices& services, MenuConversion conversion, const CommandMenuOptions& options) {
  struct Built {
    int index;
    std::vector<CommandMenuEntry> entries;
    std::vector<LabelOverride> overrides;
  };
  auto built = std::make_shared<std::vector<Built>>();
  for (size_t i = 0; i < conversion.titles.size(); ++i) {
    MenuSpec spec = buildCommandMenu(services, conversion.titles[i].entries, options);
    applyLabelOverrides(spec.items, conversion.titles[i].entries, conversion.overrides[i], services);
    const int index = bar.addMenu(conversion.titles[i].title, std::move(spec));
    if (index >= 0) built->push_back({index, std::move(conversion.titles[i].entries), std::move(conversion.overrides[i])});
  }
  const CommandServices copy = services;
  bar.setBeforeOpen([copy, built, options](int index, MenuSpec& spec) {
    for (const Built& b : *built) {
      if (b.index != index) continue;
      spec.items = buildCommandMenu(copy, b.entries, options).items;
      applyLabelOverrides(spec.items, b.entries, b.overrides, copy);
    }
  });
}

}  // namespace

// ---- menu bar -----------------------------------------------------------------------------------

void CustomizableMenuBar::onAttached() {
  style().flexShrink = 0.0;
  style().alignSelf = layout::Align::Start;
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  listener_ = controller_.subscribe([context, self] {
    CustomizableMenuBar* bar = context->objectAs<CustomizableMenuBar>(self);
    if (bar == nullptr) return;
    const bool editing = bar->controller_.editMode();
    if (editing != bar->builtEditing_ || (!editing && bar->controller_.model().version() != bar->builtVersion_)) bar->rebuild();
  });
  rebuild();
}

void CustomizableMenuBar::onDetached() { controller_.unsubscribe(listener_); }

void CustomizableMenuBar::rebuild() {
  if (child_.valid() && ui().alive(child_)) ui().destroy(child_);
  child_ = {};
  builtEditing_ = controller_.editMode();
  builtVersion_ = controller_.model().version();
  if (builtEditing_) {
    child_ = ui().create<MenuEditor>(id(), controller_).id();
    return;
  }
  MenuBar& bar = ui().create<MenuBar>(id());
  bar.style().alignSelf = layout::Align::Start;
  child_ = bar.id();
  bindMenus(bar, controller_.services(), convertMenuBar(controller_.model().effective().layout.menuBar, controller_.model()), options_);
}

// ---- toolbar ------------------------------------------------------------------------------------

void CustomizableToolbar::onAttached() {
  style().flexShrink = 0.0;
  style().alignSelf = layout::Align::Start;
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  listener_ = controller_.subscribe([context, self] {
    CustomizableToolbar* bar = context->objectAs<CustomizableToolbar>(self);
    if (bar == nullptr) return;
    const bool editing = bar->controller_.editMode();
    if (editing != bar->builtEditing_ || (!editing && bar->controller_.model().version() != bar->builtVersion_)) bar->rebuild();
  });
  rebuild();
}

void CustomizableToolbar::onDetached() { controller_.unsubscribe(listener_); }

ToolbarButton* CustomizableToolbar::button(std::string_view commandId) const {
  for (const auto& binding : bindings_) {
    if (ToolbarButton* b = binding->button(commandId)) return b;
  }
  return nullptr;
}

void CustomizableToolbar::applyLook(Toolbar& toolbar, const cz::ToolbarLayout& layoutNow) {
  const bool horizontal = toolbar.orientation() == ToolbarOrientation::Horizontal;
  if (layoutNow.gap != cz::kDefaultToolbarGap) {
    (horizontal ? toolbar.style().gapColumn : toolbar.style().gapRow) = layoutNow.gap;
  }
  if (layoutNow.sizeStep == cz::SizeStep::Medium) return;
  const double edge = cz::sizeStepPixels(layoutNow.sizeStep);
  ui().tree().forEachDescendant(toolbar.id(), [&](core::tree::WidgetId child) {
    if (ToolbarButton* b = ui().objectAs<ToolbarButton>(child)) {
      b->style().width = layout::Length::px(edge);
      b->style().height = layout::Length::px(edge);
    } else if (ToolbarTrigger* t = ui().objectAs<ToolbarTrigger>(child)) {
      (horizontal ? t->style().height : t->style().width) = layout::Length::px(edge);
    }
  });
}

void CustomizableToolbar::rebuild() {
  bindings_.clear();
  if (child_.valid() && ui().alive(child_)) ui().destroy(child_);
  child_ = {};
  builtEditing_ = controller_.editMode();
  builtVersion_ = controller_.model().version();
  if (builtEditing_) {
    child_ = ui().create<ToolbarEditor>(id(), controller_, toolbarId_).id();
    return;
  }
  const cz::ToolbarLayout* layoutNow = cz::findToolbar(controller_.model().effective().layout, toolbarId_);
  if (layoutNow == nullptr) return;
  Toolbar& toolbar = ui().create<Toolbar>(id(), layoutNow->orientation == cz::Orientation::Vertical ? ToolbarOrientation::Vertical : ToolbarOrientation::Horizontal);
  child_ = toolbar.id();
  for (const ToolbarRun& run : convertToolbar(*layoutNow)) {
    if (!run.items.empty()) bindings_.push_back(bindCommandToolbar(ui(), toolbar, controller_.sync(), run.items));
    if (run.spacerAfter) ui().create<ToolbarSpacer>(toolbar.id());
  }
  applyLook(toolbar, *layoutNow);
}

}  // namespace r1ui::widgets
