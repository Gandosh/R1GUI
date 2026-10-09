// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ColorControls.h.
// Invariants: a gesture is one begin ... end bracket; end is always called exactly once per begin
//   (pointer release, Escape, capture loss, or the keyboard's own bracket); callbacks may destroy the
//   widget, so nothing is touched after a callback unless it is still alive.
// Callers: the colour picker; tests.
#include "r1ui/widgets/colorpicker/ColorControls.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/colorpicker/NumberText.h"
#include "r1ui/widgets/colorpicker/PickerDraw.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

namespace events = core::events;

constexpr double kSquareHeight = 140.0;
constexpr double kSquareRadius = 4.0;
constexpr double kTrackHeight = 12.0;
constexpr double kTrackRadius = 6.0;
constexpr double kThumb = 14.0;
constexpr double kThumbBorder = 2.0;
constexpr double kThumbRing = 1.0;
constexpr double kRowHeight = 26.0;
constexpr double kLabelColumn = 24.0;  // 16 px label + 8 px gap
constexpr double kEntryWidth = 96.0;

render::Color toColor(const color::Rgb& c, float a = 1.0f) {
  return {static_cast<float>(c.r), static_cast<float>(c.g), static_cast<float>(c.b), a};
}

// Unit step of an arrow key: 1 unit, or 10 with Shift (a unit is passed by the caller).
double stepFor(const events::Event& e, double unit) { return ((e.modifiers & events::Mod::kShift) != 0 ? 10.0 : 1.0) * unit; }

}  // namespace

// ---- SvSquare ---------------------------------------------------------------------------------

void SvSquare::onAttached() {
  core::layout::Style& s = style();
  s.height = core::layout::Length::px(kSquareHeight);
  s.flexShrink = 0.0;
  setFocusable(true);
}

std::string_view SvSquare::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view("Saturation and value") : WidgetObject::accessibleName();
}

void SvSquare::setDisplay(double hueDegrees, double s, double v, const color::Rgb& fill) {
  const double h = color::wrapHue(hueDegrees);
  const double ns = color::clamp01(s);
  const double nv = color::clamp01(v);
  if (h == hue_ && ns == s_ && nv == v_ && fill == fill_) return;
  hue_ = h;
  s_ = ns;
  v_ = nv;
  fill_ = color::sanitized(fill);
  requestPaint();
}

void SvSquare::paint(PaintContext& ctx) {
  const render::Rect box = ctx.box();
  const float radius = ctx.px(kSquareRadius);
  const color::Rgb pure = color::toRgb(color::Hsv{hue_, 1.0, 1.0});
  pickerdraw::fillRoundedHorizontal(ctx.painter(), box, radius, [&](float t) {
    return render::Color{static_cast<float>(1.0 + (pure.r - 1.0) * t), static_cast<float>(1.0 + (pure.g - 1.0) * t),
                         static_cast<float>(1.0 + (pure.b - 1.0) * t), 1.0f};
  });
  pickerdraw::fillRoundedVertical(ctx.painter(), box, radius, [](float t) { return render::Color{0.0f, 0.0f, 0.0f, t}; });
}

void SvSquare::paintOver(PaintContext& ctx) {
  const render::Rect box = ctx.box();
  pickerdraw::drawRoundThumb(ctx, box.x + static_cast<float>(s_) * box.w, box.y + static_cast<float>(1.0 - v_) * box.h, kThumb, kThumbBorder,
                             kThumbRing, toColor(fill_));
  if (focusVisible()) ctx.focusRing(box, ctx.px(kSquareRadius));
}

void SvSquare::update(double localX, double localY) {
  const core::layout::Rect r = ui().absRect(id());
  if (r.w <= 0 || r.h <= 0) return;
  const double s = color::clamp01(localX / r.w);
  const double v = color::clamp01(1.0 - localY / r.h);
  s_ = s;
  v_ = v;
  requestPaint();
  const core::tree::WidgetId self = id();
  if (onChange) onChange(s, v);
  if (!ui().alive(self)) return;
}

// A drag whose change callback destroyed the widget (the picker's popover closing) never reached onEnd:
// the hook closes the bracket the host opened with onBegin.
void SvSquare::onDetached() {
  if (!gestureOpen_) return;
  gestureOpen_ = false;
  dragging_ = false;
  if (onEnd) onEnd();
}

void SvSquare::finish(bool cancel) {
  if (!dragging_) return;
  dragging_ = false;
  const core::tree::WidgetId self = id();
  if (cancel) {
    s_ = startS_;
    v_ = startV_;
    requestPaint();
    if (onChange) onChange(startS_, startV_);
    if (!ui().alive(self)) return;
  }
  gestureOpen_ = false;
  if (onEnd) onEnd();
}

void SvSquare::onPointerDown(Event& e) {
  if (e.button != events::Button::Left) return;
  const core::layout::Rect r = ui().absRect(id());
  if (r.w <= 0 || r.h <= 0) return;
  e.markHandled();
  if (!focused()) ui().router().focus(id(), events::FocusReason::Pointer);
  ui().router().capturePointer(id());
  dragging_ = true;
  gestureOpen_ = true;
  startS_ = s_;
  startV_ = v_;
  const core::tree::WidgetId self = id();
  if (onBegin) onBegin();
  if (!ui().alive(self)) return;
  update(e.localX, e.localY);
}

void SvSquare::onPointerMove(Event& e) {
  if (dragging_) update(e.localX, e.localY);
}

void SvSquare::onPointerUp(Event& e) {
  if (e.button == events::Button::Left) finish(false);
}

void SvSquare::onCaptureLost(Event&) { finish(true); }

void SvSquare::onKeyDown(Event& e) {
  using events::Key;
  if (e.key == Key::Escape && dragging_) {
    e.markHandled();
    finish(true);
    ui().router().releaseCapture();
    return;
  }
  double ds = 0.0;
  double dv = 0.0;
  switch (e.key) {
    case Key::Left: ds = -stepFor(e, 0.01); break;
    case Key::Right: ds = stepFor(e, 0.01); break;
    case Key::Up: dv = stepFor(e, 0.01); break;
    case Key::Down: dv = -stepFor(e, 0.01); break;
    case Key::PageUp: dv = 0.1; break;
    case Key::PageDown: dv = -0.1; break;
    default: return;
  }
  e.markHandled();
  if (dragging_) return;
  const core::tree::WidgetId self = id();
  const double s = color::clamp01(s_ + ds);
  const double v = color::clamp01(v_ + dv);
  if (s == s_ && v == v_) return;
  s_ = s;
  v_ = v;
  requestPaint();
  if (onBegin) onBegin();
  if (!ui().alive(self)) return;
  if (onChange) onChange(s, v);
  if (!ui().alive(self)) return;
  if (onEnd) onEnd();
}

// ---- ColorSliderTrack -------------------------------------------------------------------------

void ColorSliderTrack::onAttached() {
  core::layout::Style& s = style();
  s.height = core::layout::Length::px(kThumb);
  s.flexGrow = 1.0;
  s.flexShrink = 1.0;
  setFocusable(true);
}

std::string_view ColorSliderTrack::accessibleName() const {
  if (!WidgetObject::accessibleName().empty()) return WidgetObject::accessibleName();
  return kind_ == Kind::Hue ? "Hue" : "Alpha";
}

void ColorSliderTrack::setPosition(double t) {
  const double n = color::clamp01(t);
  if (n == t_) return;
  t_ = n;
  requestPaint();
}

void ColorSliderTrack::setBase(const color::Rgb& base) {
  const color::Rgb c = color::sanitized(base);
  if (c == base_) return;
  base_ = c;
  requestPaint();
}

double ColorSliderTrack::thumbCentreX(double w) const { return kThumb * 0.5 + t_ * std::max(0.0, w - kThumb); }

double ColorSliderTrack::positionAt(double localX) const {
  const core::layout::Rect r = ui().absRect(id());
  const double travel = r.w - kThumb;
  if (travel <= 0.0) return 0.0;
  return color::clamp01((localX - kThumb * 0.5) / travel);
}

void ColorSliderTrack::paint(PaintContext& ctx) {
  const render::Rect all = ctx.box();
  const float trackH = ctx.px(kTrackHeight);
  const render::Rect box{all.x, all.y + (all.h - trackH) * 0.5f, all.w, trackH};
  const float radius = ctx.px(kTrackRadius);
  if (kind_ == Kind::Hue) {
    pickerdraw::fillRoundedHorizontal(ctx.painter(), box, radius, [](float t) {
      const color::Rgb c = color::toRgb(color::Hsv{static_cast<double>(t) * 360.0, 1.0, 1.0});
      return toColor(c);
    });
  } else {
    pickerdraw::drawCheckerboard(ctx, box, radius, 4.0, ctx.color("checkerboard"), render::Color::fromRgba8(0xcc, 0xcc, 0xcc));
    pickerdraw::fillRoundedHorizontal(ctx.painter(), box, radius, [&](float t) { return toColor(base_, t); });
  }
}

void ColorSliderTrack::paintOver(PaintContext& ctx) {
  const render::Rect all = ctx.box();
  const core::layout::Rect r = ui().absRect(id());
  const float cx = all.x + ctx.px(thumbCentreX(r.w));
  const float cy = all.y + all.h * 0.5f;
  const color::Rgb fill = kind_ == Kind::Hue ? color::toRgb(color::Hsv{t_ * 360.0, 1.0, 1.0}) : base_;
  pickerdraw::drawRoundThumb(ctx, cx, cy, kThumb, kThumbBorder, kThumbRing, toColor(fill));
  if (focusVisible()) ctx.focusRing(all, ctx.px(kThumb * 0.5));
}

void ColorSliderTrack::onDetached() {
  if (!gestureOpen_) return;
  gestureOpen_ = false;
  dragging_ = false;
  if (onEnd) onEnd();
}

void ColorSliderTrack::finish(bool cancel) {
  if (!dragging_) return;
  dragging_ = false;
  const core::tree::WidgetId self = id();
  if (cancel) {
    t_ = startT_;
    requestPaint();
    if (onChange) onChange(startT_);
    if (!ui().alive(self)) return;
  }
  gestureOpen_ = false;
  if (onEnd) onEnd();
}

void ColorSliderTrack::onPointerDown(Event& e) {
  if (e.button != events::Button::Left) return;
  const core::layout::Rect r = ui().absRect(id());
  if (r.w <= 0 || r.h <= 0) return;
  e.markHandled();
  if (!focused()) ui().router().focus(id(), events::FocusReason::Pointer);
  ui().router().capturePointer(id());
  dragging_ = true;
  gestureOpen_ = true;
  startT_ = t_;
  const core::tree::WidgetId self = id();
  if (onBegin) onBegin();
  if (!ui().alive(self)) return;
  t_ = positionAt(e.localX);
  requestPaint();
  if (onChange) onChange(t_);
}

void ColorSliderTrack::onPointerMove(Event& e) {
  if (!dragging_) return;
  t_ = positionAt(e.localX);
  requestPaint();
  if (onChange) onChange(t_);
}

void ColorSliderTrack::onPointerUp(Event& e) {
  if (e.button == events::Button::Left) finish(false);
}

void ColorSliderTrack::onCaptureLost(Event&) { finish(true); }

void ColorSliderTrack::onKeyDown(Event& e) {
  using events::Key;
  if (e.key == Key::Escape && dragging_) {
    e.markHandled();
    finish(true);
    ui().router().releaseCapture();
    return;
  }
  // One unit is a degree on the hue track and a percent on the alpha track.
  const double unit = kind_ == Kind::Hue ? 1.0 / 360.0 : 0.01;
  double next = t_;
  switch (e.key) {
    case Key::Left:
    case Key::Down: next = t_ - stepFor(e, unit); break;
    case Key::Right:
    case Key::Up: next = t_ + stepFor(e, unit); break;
    case Key::PageDown: next = t_ - 10.0 * unit; break;
    case Key::PageUp: next = t_ + 10.0 * unit; break;
    case Key::Home: next = 0.0; break;
    case Key::End: next = 1.0; break;
    default: return;
  }
  e.markHandled();
  if (dragging_) return;
  // The hue track wraps nowhere: 360 degrees is the same colour as 0, but the track stops at its end.
  next = color::clamp01(next);
  if (next == t_) return;
  t_ = next;
  requestPaint();
  const core::tree::WidgetId self = id();
  if (onBegin) onBegin();
  if (!ui().alive(self)) return;
  if (onChange) onChange(t_);
  if (!ui().alive(self)) return;
  if (onEnd) onEnd();
}

// ---- ColorSliderRow ---------------------------------------------------------------------------

void ColorSliderRow::onAttached() {
  core::layout::Style& s = style();
  s.direction = core::layout::FlexDirection::Row;
  s.alignItems = core::layout::Align::Center;
  s.height = core::layout::Length::px(kRowHeight);
  s.flexShrink = 0.0;
  s.gapColumn = 8.0;
  s.padding[core::layout::kLeft] = kLabelColumn;

  ColorSliderTrack& track = ui().create<ColorSliderTrack>(id(), kind_);
  track_ = track.id();
  track.onBegin = [this] {
    if (onBegin) onBegin();
  };
  track.onChange = [this](double t) {
    value_ = kind_ == ColorSliderTrack::Kind::Hue ? color::wrapHue(t * 360.0) : t;
    refreshEntry();
    if (onChange) onChange(value_);
  };
  track.onEnd = [this] {
    if (onEnd) onEnd();
  };

  PickerEntry& entry = ui().create<PickerEntry>(id(), PickerEntry::Look::Field);
  entry_ = entry.id();
  entry.style().width = core::layout::Length::px(kEntryWidth);
  entry.style().flexShrink = 0.0;
  entry.setPadLeft(2.0);
  entry.setFontSize(13.0);
  if (kind_ == ColorSliderTrack::Kind::Alpha) entry.setSuffix("%");
  entry.setAccessibleName(kind_ == ColorSliderTrack::Kind::Hue ? "Hue value" : "Alpha percent");
  entry.onCommit = [this](std::string_view text) { return commitText(text); };
  entry.onStep = [this](int steps) {
    const double unit = kind_ == ColorSliderTrack::Kind::Hue ? 1.0 : 0.01;
    const double next = kind_ == ColorSliderTrack::Kind::Hue ? color::wrapHue(value_ + steps * unit) : color::clamp01(value_ + steps * unit);
    if (next == value_) return;
    if (onBegin) onBegin();
    value_ = next;
    ColorSliderTrack* tr = ui().objectAs<ColorSliderTrack>(track_);
    if (tr != nullptr) tr->setPosition(kind_ == ColorSliderTrack::Kind::Hue ? value_ / 360.0 : value_);
    refreshEntry();
    if (onChange) onChange(value_);
    if (onEnd) onEnd();
  };
  refreshEntry();
}

ColorSliderTrack& ColorSliderRow::track() const { return *ui().objectAs<ColorSliderTrack>(track_); }
PickerEntry& ColorSliderRow::entry() const { return *ui().objectAs<PickerEntry>(entry_); }

void ColorSliderRow::refreshEntry() {
  if (PickerEntry* e = ui().objectAs<PickerEntry>(entry_)) {
    const double shown = kind_ == ColorSliderTrack::Kind::Hue ? value_ : value_ * 100.0;
    e->setText(formatNumber(std::round(shown), 0));
  }
}

bool ColorSliderRow::commitText(std::string_view text) {
  const std::optional<double> parsed = parseNumber(text);
  if (!parsed) return false;
  const bool hue = kind_ == ColorSliderTrack::Kind::Hue;
  const double next = hue ? color::wrapHue(*parsed) : color::clamp01(*parsed / 100.0);
  const core::tree::WidgetId self = id();
  if (onBegin) onBegin();
  if (!ui().alive(self)) return true;
  value_ = next;
  track().setPosition(hue ? value_ / 360.0 : value_);
  refreshEntry();
  if (onChange) onChange(value_);
  if (!ui().alive(self)) return true;
  if (onEnd) onEnd();
  return true;
}

void ColorSliderRow::setHue(double degrees) {
  value_ = color::wrapHue(degrees);
  track().setPosition(value_ / 360.0);
  refreshEntry();
}

void ColorSliderRow::setAlpha(double alpha) {
  value_ = color::clamp01(alpha);
  track().setPosition(value_);
  refreshEntry();
}

void ColorSliderRow::setBase(const color::Rgb& base) { track().setBase(base); }

void ColorSliderRow::paint(PaintContext& ctx) {
  const char* label = kind_ == ColorSliderTrack::Kind::Hue ? "Hue" : "Alpha";
  const render::Rect box = ctx.box();
  TextOptions options;
  options.ellipsis = false;
  options.color = ctx.color("muted");
  options.weight = 500;
  theme::TextStyle ts = ctx.resolve("label.caption", 0).text;
  ts.fontSize = 10.0;
  ts.weight = 500;
  // The label is wider than its 16 px column ("Alpha"): it overflows under the track, which paints
  // after the row. drawText clips to the box it is given, so hand it one wide enough.
  render::Rect wide = box;
  wide.w = ctx.px(kLabelColumn + 8.0);
  ctx.drawText(label, ts, wide, options);
}

}  // namespace r1ui::widgets
