// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of KeyboardView (KeyboardView.h): metrics, painting of caps and legend,
//   hit-testing, hover tooltip text and the modifier layer.
// Invariants: usage_ is recomputed only by refresh() (called when the filter, the layer, the selection
//   or the bindings change); painting never reads the keymap; the unit size is positive or nothing is
//   drawn; hover_ is -1 or a valid cap index.
// Callers: HotkeyEditor, tests.
#include "r1ui/widgets/hotkeys/KeyboardView.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace cmd = commands;
using core::events::Button;

namespace {

constexpr double kPad = 2.0;
constexpr double kLegendUnits = 1.3;  // height reserved under the board, in key units
constexpr double kNaturalWidth = 640.0;

const std::vector<cmd::KeyUse>& noUses() {
  static const std::vector<cmd::KeyUse> empty;
  return empty;
}

const char* modifierName(cmd::ModifierRole role) {
  switch (role) {
    case cmd::ModifierRole::Shift: return "Shift";
    case cmd::ModifierRole::Ctrl: return "Ctrl";
    case cmd::ModifierRole::Alt: return "Alt";
    case cmd::ModifierRole::Meta: return "Meta";
    case cmd::ModifierRole::None: break;
  }
  return "";
}

}  // namespace

void KeyboardView::onAttached() {
  setFocusable(true);
  style().alignSelf = layout::Align::Stretch;
  style().flexShrink = 0.0;
  style().hasMeasure = true;
  refresh();
}

// The height follows the width so the board keeps its proportions (no fixed ratio style: a stretched
// width must give the height, not the other way round).
layout::MeasureResult KeyboardView::measure(const layout::MeasureInput& input) {
  const double width = input.widthMode == layout::MeasureMode::Undefined ? kNaturalWidth : std::max(0.0, input.width);
  const double unit = std::max(0.0, (width - 2 * kPad) / layout_.width());
  return {width, unit * (layout_.height() + kLegendUnits) + 2 * kPad};
}

// ---- state --------------------------------------------------------------------------------------------

void KeyboardView::setUsageFilter(cmd::KeyUsageFilter filter) {
  if (filter.category == filter_.category && filter.context == filter_.context) return;
  filter_ = std::move(filter);
  refresh();
}

void KeyboardView::setSelectedCommand(std::string commandId) {
  if (commandId == selected_) return;
  selected_ = std::move(commandId);
  requestPaint();
}

void KeyboardView::setToggledModifiers(uint8_t modifiers) {
  modifiers &= cmd::kAllModifiers;
  if (modifiers == toggled_) return;
  toggled_ = modifiers;
  modifiersChanged();
}

void KeyboardView::setPhysicalModifiers(uint8_t modifiers) {
  modifiers &= cmd::kAllModifiers;
  if (modifiers == physical_) return;
  physical_ = modifiers;
  modifiersChanged();
}

void KeyboardView::modifiersChanged() {
  refresh();
  if (onModifiersChanged_) {
    auto callback = onModifiersChanged_;
    callback(effectiveModifiers());
  }
}

void KeyboardView::refresh() {
  usage_ = cmd::collectKeyUsage(services_.registry, services_.keymap, filter_, effectiveModifiers());
  tip_.clear();
  requestPaint();
}

const std::vector<cmd::KeyUse>& KeyboardView::usesOf(cmd::Key key) const {
  const auto it = usage_.find(static_cast<uint16_t>(key));
  return it == usage_.end() ? noUses() : it->second;
}

bool KeyboardView::holdsSelected(cmd::Key key) const {
  if (selected_.empty()) return false;
  const std::vector<cmd::KeyUse>& uses = usesOf(key);
  return std::any_of(uses.begin(), uses.end(), [&](const cmd::KeyUse& u) { return u.commandId == selected_; });
}

// ---- geometry ----------------------------------------------------------------------------------------------

KeyboardView::Metrics KeyboardView::metrics() const {
  const layout::Rect r = ui().absRect(id());
  Metrics m;
  const double byWidth = (r.w - 2 * kPad) / layout_.width();
  const double byHeight = (r.h - 2 * kPad) / (layout_.height() + kLegendUnits);
  m.unit = std::max(0.0, std::min(byWidth, byHeight));
  m.x = r.x + (r.w - m.unit * layout_.width()) * 0.5;
  m.y = r.y + kPad;
  return m;
}

layout::RectD KeyboardView::capRect(size_t index) const {
  if (index >= layout_.caps().size()) return {};
  const Metrics m = metrics();
  const cmd::KeyCap& c = layout_.caps()[index];
  return {m.x + c.x * m.unit, m.y + c.y * m.unit, c.w * m.unit, c.h * m.unit};
}

int KeyboardView::capAt(double x, double y) const {
  const Metrics m = metrics();
  if (m.unit <= 0.0 || !std::isfinite(x) || !std::isfinite(y)) return -1;
  return layout_.capAt(static_cast<float>((x - m.x) / m.unit), static_cast<float>((y - m.y) / m.unit));
}

std::string KeyboardView::describe(size_t index) const {
  if (index >= layout_.caps().size()) return {};
  const cmd::KeyCap& cap = layout_.caps()[index];
  if (cap.modifier != cmd::ModifierRole::None) return std::string(modifierName(cap.modifier)) + ": click to include it in the layer shown";
  if (!cap.bindable()) return std::string(cap.label) + ": cannot hold a shortcut";
  const std::string layer = cmd::formatChord(cmd::KeyChord{cap.key, effectiveModifiers(), false});
  const std::vector<cmd::KeyUse>& uses = usesOf(cap.key);
  if (uses.empty()) return layer + ": unassigned";
  std::string text;
  for (const cmd::KeyUse& use : uses) {
    const cmd::CommandDef* def = services_.registry.find(use.commandId);
    if (!text.empty()) text += '\n';
    text += cmd::formatSequence(use.sequence) + ": " + (def != nullptr ? def->label : use.commandId);
    if (use.context != cmd::kGlobalContext) text += " [" + use.context + "]";
  }
  return text;
}

std::string_view KeyboardView::tooltipText() const {
  if (hover_ < 0) return {};
  tip_ = describe(static_cast<size_t>(hover_));
  return tip_;
}

Cursor KeyboardView::cursor() const {
  if (hover_ < 0) return Cursor::Default;
  const cmd::KeyCap& cap = layout_.caps()[static_cast<size_t>(hover_)];
  return cap.bindable() || cap.modifier != cmd::ModifierRole::None ? Cursor::Pointer : Cursor::Default;
}

// ---- painting ------------------------------------------------------------------------------------------------

void KeyboardView::paint(PaintContext& ctx) {
  const Metrics m = metrics();
  if (m.unit <= 4.0) return;
  render::Painter& painter = ctx.painter();
  const theme::TextStyle base = ctx.style("label.body").text;
  theme::TextStyle keyText = base;
  keyText.fontSize = std::clamp(m.unit * 0.36, 8.0, 14.0);
  keyText.weight = 500;
  const render::Color white{1.0f, 1.0f, 1.0f, 1.0f};
  const double gap = std::max(2.0, m.unit * 0.09);
  const float radius = ctx.px(std::clamp(m.unit * 0.14, 3.0, 7.0));
  const uint8_t layer = effectiveModifiers();

  for (size_t i = 0; i < layout_.caps().size(); ++i) {
    const cmd::KeyCap& cap = layout_.caps()[i];
    const layout::RectD cell = capRect(i);
    const render::Rect box = ctx.toPhysical(cell.x + gap * 0.5, cell.y + gap * 0.5, cell.w - gap, cell.h - gap);
    const bool isModifier = cap.modifier != cmd::ModifierRole::None;
    const bool active = isModifier && (cmd::modifierBit(cap.modifier) & layer) != 0;
    const bool hasUse = cap.bindable() && assigned(cap.key);
    const bool selected = hasUse && holdsSelected(cap.key);
    const bool dead = !cap.bindable() && !isModifier;  // no Key code: cannot hold a binding
    const float opacity = dead ? 0.45f : 1.0f;
    if (opacity < 1.0f) painter.pushOpacity(opacity);

    render::Color fill = ctx.color("input");
    render::Color border = ctx.color("border");
    render::Color text = ctx.color(base.color, 0.75);
    if (hasUse) {
      fill = ctx.color("accent");
      border = ctx.color("accent");
      text = white;
    } else if (active) {
      fill = ctx.color("accent", 0.28);
      border = ctx.color("accent");
      text = ctx.color(base.color);
    }
    painter.fillRoundedRect(box, render::CornerRadii::uniform(radius), fill);
    painter.border(box, render::CornerRadii::uniform(radius), ctx.hairline(), border);
    if (static_cast<int>(i) == hover_ && !dead) painter.fillRoundedRect(box, render::CornerRadii::uniform(radius), render::Color{1.0f, 1.0f, 1.0f, hasUse ? 0.16f : 0.10f});
    if (selected) painter.border(box, render::CornerRadii::uniform(radius), ctx.px(2.0), ctx.color(base.color));
    // A long label ("Backspace", "Right") shrinks to fit its cap instead of being cut.
    theme::TextStyle label = keyText;
    const double wanted = static_cast<double>(ui().text().measure(cap.label, static_cast<float>(label.fontSize * ctx.scale()), label.weight)) / ctx.scale();
    const double room = std::max(1.0, cell.w - gap - 4.0);
    if (wanted > room) label.fontSize = std::max(6.0, label.fontSize * room / wanted);
    TextOptions options;
    options.align = TextAlign::Center;
    options.color = text;
    options.padLeft = options.padRight = 1.0;
    ctx.drawText(cap.label, label, box, options);
    if (opacity < 1.0f) painter.popOpacity();
  }

  // Legend under the board: two swatches with their meaning.
  const double legendY = m.y + layout_.height() * m.unit + m.unit * 0.35;
  const double swatch = std::clamp(m.unit * 0.42, 8.0, 14.0);
  theme::TextStyle legend = ctx.style("label.muted").text;
  legend.fontSize = std::clamp(m.unit * 0.38, 9.0, 13.0);
  double x = m.x + m.unit * 0.2;
  const struct {
    bool assignedSwatch;
    const char* text;
  } items[] = {{true, "Assigned Key"}, {false, "Unassigned Key"}};
  for (const auto& item : items) {
    const render::Rect box = ctx.toPhysical(x, legendY, swatch, swatch);
    painter.fillRoundedRect(box, render::CornerRadii::uniform(ctx.px(3.0)), item.assignedSwatch ? ctx.color("accent") : ctx.color("input"));
    if (!item.assignedSwatch) painter.border(box, render::CornerRadii::uniform(ctx.px(3.0)), ctx.hairline(), ctx.color("border"));
    const double textW = 110.0;
    TextOptions o;
    ctx.drawText(item.text, legend, ctx.toPhysical(x + swatch + 6.0, legendY - 3.0, textW, swatch + 6.0), o);
    x += swatch + 6.0 + textW;
  }
}

void KeyboardView::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(4.0));
}

// ---- input ---------------------------------------------------------------------------------------------------------

void KeyboardView::onPointerMove(Event& e) {
  const int cap = capAt(e.x, e.y);
  if (cap != hover_) {
    hover_ = cap;
    requestPaint();
  }
}

void KeyboardView::onPointerLeave(Event&) {
  if (hover_ != -1) {
    hover_ = -1;
    requestPaint();
  }
}

void KeyboardView::onClick(Event& e) {
  if (e.button != Button::Left) return;
  ui().router().focus(id(), core::events::FocusReason::Pointer);
  const int index = capAt(e.x, e.y);
  if (index < 0) return;
  const cmd::KeyCap& cap = layout_.caps()[static_cast<size_t>(index)];
  e.markHandled();
  if (cap.modifier != cmd::ModifierRole::None) {
    setToggledModifiers(static_cast<uint8_t>(toggled_ ^ cmd::modifierBit(cap.modifier)));
    return;
  }
  if (!cap.bindable() || !onKeyClicked_) return;
  auto callback = onKeyClicked_;
  callback(cap.key, effectiveModifiers());
}

void KeyboardView::onKeyDown(Event& e) { setPhysicalModifiers(e.modifiers); }
void KeyboardView::onKeyUp(Event& e) { setPhysicalModifiers(e.modifiers); }
void KeyboardView::onFocusOut(Event&) { setPhysicalModifiers(0); }

}  // namespace r1ui::widgets
