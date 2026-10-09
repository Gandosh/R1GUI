// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ToastParts.h.
// Invariants: measure() and paint() use the same wrapped lines (one wrap width, derived from the
//   maximum toast width and the controls), so a toast never clips its own text; the toast never
//   mutates the tree while painting; callbacks run after the widget updated its own state.
// Callers: ToastManager, UiContext (dispatch), tests.
#include "r1ui/widgets/toast/ToastParts.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/tooltip/TooltipContent.h"

namespace r1ui::widgets {

namespace {

using theme::State::kNone;
using theme::StyleProperty;

constexpr theme::StyleRuleEntry kRows[] = {
    {"toast.default", kNone, StyleProperty::Background, "color:accent"},
    {"toast.default", kNone, StyleProperty::Foreground, "#ffffff"},
    {"toast.default", kNone, StyleProperty::BorderWidth, "number:0"},
    {"toast.default", kNone, StyleProperty::Radius, "radius:md"},
    {"toast.default", kNone, StyleProperty::PaddingX, "number:10"},
    {"toast.default", kNone, StyleProperty::PaddingY, "number:6"},
    {"toast.default", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"toast.default", kNone, StyleProperty::LineHeight, "number:16"},
    {"toast.warning", kNone, StyleProperty::Background, "color:warning-bg"},
    {"toast.warning", kNone, StyleProperty::Foreground, "color:warning-text"},
    {"toast.warning", kNone, StyleProperty::BorderColor, "color:warning-border"},
    {"toast.warning", kNone, StyleProperty::BorderWidth, "number:1"},
    {"toast.warning", kNone, StyleProperty::Radius, "radius:md"},
    {"toast.warning", kNone, StyleProperty::PaddingX, "number:10"},
    {"toast.warning", kNone, StyleProperty::PaddingY, "number:6"},
    {"toast.warning", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"toast.warning", kNone, StyleProperty::LineHeight, "number:16"},
    {"toast.error", kNone, StyleProperty::Background, "#e7000b"},
    {"toast.error", kNone, StyleProperty::Foreground, "#ffffff"},
    {"toast.error", kNone, StyleProperty::BorderWidth, "number:0"},
    {"toast.error", kNone, StyleProperty::Radius, "radius:md"},
    {"toast.error", kNone, StyleProperty::PaddingX, "number:10"},
    {"toast.error", kNone, StyleProperty::PaddingY, "number:6"},
    {"toast.error", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"toast.error", kNone, StyleProperty::LineHeight, "number:16"},
};

constexpr double kIconSize = 12.0;
constexpr double kGap = 6.0;
constexpr double kButtonWidth = 16.0;   // a 12 px icon with 2 px around it (measured: 22 px between button centres)
constexpr double kButtonHeight = 18.0;
constexpr size_t kMaxLines = 12;

double widthOf(UiContext& ui, std::string_view text, double fontSize, int weight) {
  if (text.empty()) return 0.0;
  const double scale = ui.scale();
  return static_cast<double>(ui.text().measure(text, static_cast<float>(fontSize * scale), weight)) / scale;
}

}  // namespace

// ---- ToastLayer ---------------------------------------------------------------------------------

void ToastLayer::onAttached() {
  core::layout::Style& s = style();
  s.position = core::layout::Position::Absolute;
  for (int e = 0; e < 4; ++e) s.inset[e] = core::layout::Length::px(0);
  s.direction = core::layout::FlexDirection::Column;
  s.alignItems = core::layout::Align::Center;
  s.justifyContent = core::layout::Justify::Start;
  s.padding[core::layout::kTop] = kToastTopMargin;
  s.gapRow = kToastGap;
  node().layer = 1;                        // above the popup hosts in the overlay layer
  node().flags.hitTestTransparent = true;  // only the toasts take the pointer
}

// ---- ToastWidget --------------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> ToastWidget::styleRows() { return kRows; }

ToastWidget::ToastWidget(std::string text, ToastTone tone, std::string icon, ToastControls controls, uint64_t fadeMs, Callbacks callbacks)
    : text_(std::move(text)), tone_(tone), icon_(std::move(icon)), controls_(controls), fadeMs_(fadeMs), callbacks_(std::move(callbacks)) {
  if (controls_ == ToastControls::Auto) controls_ = tone_ == ToastTone::Error ? ToastControls::CopyAndClose : ToastControls::None;
  if (icon_.empty()) icon_ = tone_ == ToastTone::Default ? "check" : "triangle-alert";
}

const char* ToastWidget::styleKey() const {
  switch (tone_) {
    case ToastTone::Warning: return "toast.warning";
    case ToastTone::Error: return "toast.error";
    case ToastTone::Default: break;
  }
  return "toast.default";
}

void ToastWidget::onAttached() {
  core::layout::Style& s = style();
  s.hasMeasure = true;
  s.flexShrink = 0.0;
  s.maxWidth = core::layout::Length::px(kToastMaxWidth);
  createdMs_ = ui().now();
}

double ToastWidget::textRoom() const {
  const theme::ResolvedStyle& rs = ui().services().resolve(styleKey(), 0);
  const double pad = rs.paddingX + rs.border.width;
  double room = kToastMaxWidth - 2.0 * pad - kIconSize - kGap;
  if (hasClose()) room -= kGap + kButtonWidth;
  if (hasCopy()) room -= kGap + kButtonWidth;
  return std::max(24.0, room);
}

const ToastWidget::Layout& ToastWidget::layoutFor(double wrapWidth) const {
  UiContext& u = ui();
  if (layout_.scale == u.scale() && layout_.wrapWidth == wrapWidth) return layout_;
  const theme::ResolvedStyle& rs = u.services().resolve(styleKey(), 0);
  Layout l;
  l.scale = u.scale();
  l.wrapWidth = wrapWidth;
  l.lines = wrapTooltipText(u, text_, rs.text.fontSize, rs.text.weight, wrapWidth, kMaxLines);
  if (l.lines.empty()) l.lines.emplace_back();
  for (const std::string& line : l.lines) l.textWidth = std::max(l.textWidth, widthOf(u, line, rs.text.fontSize, rs.text.weight));
  layout_ = std::move(l);
  return layout_;
}

core::layout::MeasureResult ToastWidget::measure(const core::layout::MeasureInput&) {
  const theme::ResolvedStyle& rs = ui().services().resolve(styleKey(), 0);
  const Layout& l = layoutFor(textRoom());
  const double pad = rs.border.width;
  double width = 2.0 * (rs.paddingX + pad) + kIconSize + kGap + l.textWidth;
  if (hasCopy()) width += kGap + kButtonWidth;
  if (hasClose()) width += kGap + kButtonWidth;
  double content = rs.text.lineHeight * static_cast<double>(l.lines.size());
  if (hasClose()) content = std::max(content, kButtonHeight);
  return {std::min(width, kToastMaxWidth), content + 2.0 * (rs.paddingY + pad)};
}

float ToastWidget::paintOpacity() const {
  if (!ui().animationsActive() || fadeMs_ == 0) return 1.0f;
  const double fade = static_cast<double>(fadeMs_);
  double value = 1.0;
  if (closing_) value = 1.0 - static_cast<double>(ui().now() - closeMs_) / fade;
  else if (ui().now() - createdMs_ < fadeMs_) value = static_cast<double>(ui().now() - createdMs_) / fade;
  else return 1.0f;
  ui().invalidator().requestAnimation(id());
  return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

void ToastWidget::beginClose() {
  if (closing_) return;
  closing_ = true;
  closeMs_ = ui().now();
  requestPaint();
}

core::layout::RectD ToastWidget::buttonRect(int button) const {
  if (button == 1 && !hasClose()) return {};
  if (button == 0 && !hasCopy()) return {};
  if (button < 0 || button > 1) return {};
  const theme::ResolvedStyle& rs = ui().services().resolve(styleKey(), 0);
  const core::layout::Rect self = ui().absRect(id());
  const double pad = rs.paddingX + rs.border.width;
  const double right = static_cast<double>(self.w) - pad;
  const double closeX = right - kButtonWidth;
  const double x = button == 1 ? closeX : closeX - kGap - kButtonWidth;
  const double top = rs.paddingY + rs.border.width;
  return {x, top, kButtonWidth, kButtonHeight};
}

int ToastWidget::buttonAt(double x, double y) const {
  for (int b = 0; b < 2; ++b) {
    const core::layout::RectD r = buttonRect(b);
    if (r.w > 0.0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return b;
  }
  return -1;
}

void ToastWidget::setHoverButton(int button) {
  if (button == hoverButton_) return;
  hoverButton_ = button;
  requestPaint();
}

void ToastWidget::onPointerEnter(Event&) {
  if (callbacks_.onHover) callbacks_.onHover(true);
}

void ToastWidget::onPointerLeave(Event&) {
  setHoverButton(-1);
  if (callbacks_.onHover) callbacks_.onHover(false);
}

void ToastWidget::onPointerMove(Event& e) { setHoverButton(buttonAt(e.localX, e.localY)); }

void ToastWidget::onClick(Event& e) {
  if (e.button != core::events::Button::Left) return;
  const int button = buttonAt(e.localX, e.localY);
  if (button < 0) return;
  e.markHandled();
  const std::function<void()> fn = button == 0 ? callbacks_.onCopy : callbacks_.onClose;  // the callback may destroy the toast
  if (fn) fn();
}

void ToastWidget::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.resolve(styleKey(), 0);
  const render::Rect box = ctx.box();
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(rs.radius));
  if (const auto layers = ctx.ui().services().tokens().shadow("md")) {
    // Issued last to first so the first listed layer ends up on top (Painter.h).
    for (auto it = layers->rbegin(); it != layers->rend(); ++it) {
      render::ShadowSpec spec;
      spec.offsetX = ctx.px(it->offsetX);
      spec.offsetY = ctx.px(it->offsetY);
      spec.blur = ctx.px(it->blur);
      spec.spread = ctx.px(it->spread);
      spec.color = ctx.color(it->color);
      ctx.painter().shadow(box, radii, spec);
    }
  }
  ctx.fillBox(rs);

  const Layout& l = layoutFor(textRoom());
  const double pad = rs.border.width;
  const double left = rs.paddingX + pad;
  const double top = rs.paddingY + pad;
  const render::Color fg = ctx.color(rs.text.color);
  const double content = ctx.rect().h - 2.0 * top;
  // Icon: 12 px, centred on the first line.
  const render::Rect iconBox{box.x + ctx.px(left), box.y + ctx.px(top), ctx.px(kIconSize), ctx.px(rs.text.lineHeight)};
  ctx.drawIcon(icon_, kIconSize, iconBox, fg);
  // Text lines, vertically centred when the buttons make the row taller than one line.
  const double textHeight = rs.text.lineHeight * static_cast<double>(l.lines.size());
  float y = box.y + ctx.px(top + std::max(0.0, (content - textHeight) * 0.5));
  const float lineH = ctx.px(rs.text.lineHeight);
  const float textX = box.x + ctx.px(left + kIconSize + kGap);
  TextOptions o;
  o.ellipsis = false;
  for (const std::string& line : l.lines) {
    ctx.drawText(line, rs.text, {textX, y, ctx.px(l.textWidth) + 2.0f, lineH}, o);
    y += lineH;
  }
  // Buttons: icons at 70% (full while hovered) with a faint round highlight under the hovered one.
  for (int b = 0; b < 2; ++b) {
    const core::layout::RectD r = buttonRect(b);
    if (r.w <= 0.0) continue;
    const render::Rect slot = ctx.toPhysical(r.x + ctx.rect().x, r.y + ctx.rect().y, r.w, r.h);
    const bool hot = hoverButton_ == b;
    if (hot) {
      const float pill = ctx.px(r.w);
      ctx.painter().fillRoundedRect({slot.x + (slot.w - pill) * 0.5f, slot.y, pill, slot.h}, render::CornerRadii::uniform(ctx.px(4.0)), render::Color{fg.r, fg.g, fg.b, 0.18f * fg.a});
    }
    render::Color tint = fg;
    tint.a *= hot ? 1.0f : 0.8f;
    ctx.drawIcon(b == 0 ? "copy" : "x", kIconSize, slot, tint);
  }
}

}  // namespace r1ui::widgets
