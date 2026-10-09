// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Painter.h: argument validation, geometry normalisation (CSS radius
//   scaling, border and shadow boxes), clip culling, instance packing and batch merging.
// Callers: RenderTarget users and unit tests. Pure CPU; the only dependency is the SDF reference
//   for radius normalisation, so CPU and shader geometry cannot diverge.
// Batching rule: a draw extends the previous batch when pipeline kind, scissor and texture are all
//   equal; anything else starts a new batch. Order of instances is always the order of calls.
#include "r1ui/render/Painter.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "r1ui/core/CheckedCast.h"
#include "r1ui/render/SdfReference.h"

namespace r1ui::render {

namespace {

// Coordinates beyond this are clamped: float precision at +-2^16 is still 1/128 px, and shader
// arithmetic on larger values would lose the anti-aliasing ramp.
constexpr double kMaxCoordinate = 65536.0;
// Blur beyond this has no visible effect difference and would make the shadow quad enormous.
constexpr float kMaxBlur = 8192.0f;
// Below this sigma the shadow is a hard-edged shape and takes the plain coverage path.
constexpr float kShadowGaussianSigmaFloor = 0.25f;

bool allFinite(std::initializer_list<float> values) {
  return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
}

bool finiteColor(const Color& c) { return allFinite({c.r, c.g, c.b, c.a}); }

float unit(float v) { return std::clamp(v, 0.0f, 1.0f); }

// Rect with its four edges clamped to +-kMaxCoordinate (edges, not size, so position stays right).
Rect clampRect(const Rect& r) {
  const double x0 = std::clamp<double>(r.x, -kMaxCoordinate, kMaxCoordinate);
  const double y0 = std::clamp<double>(r.y, -kMaxCoordinate, kMaxCoordinate);
  const double x1 = std::clamp(static_cast<double>(r.x) + r.w, -kMaxCoordinate, kMaxCoordinate);
  const double y1 = std::clamp(static_cast<double>(r.y) + r.h, -kMaxCoordinate, kMaxCoordinate);
  return Rect{static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1 - x0),
              static_cast<float>(y1 - y0)};
}

Rect inflate(const Rect& r, float amount) {
  return Rect{r.x - amount, r.y - amount, r.w + 2.0f * amount, r.h + 2.0f * amount};
}

void setRect(float (&dst)[4], const Rect& r) {
  dst[0] = r.x;
  dst[1] = r.y;
  dst[2] = r.w;
  dst[3] = r.h;
}

void setRadii(float (&dst)[4], const CornerRadii& r) {
  dst[0] = r.topLeft;
  dst[1] = r.topRight;
  dst[2] = r.bottomRight;
  dst[3] = r.bottomLeft;
}

void setColor(float (&dst)[4], const Color& c, float alpha) {
  dst[0] = unit(c.r);
  dst[1] = unit(c.g);
  dst[2] = unit(c.b);
  dst[3] = alpha;
}

}  // namespace

float DisplayScale::snapped(float logical) const {
  const float physical = toPhysical(logical);
  if (!(physical > 0.0f)) return 0.0f;  // also catches NaN
  return std::max(1.0f, std::round(physical));
}

Painter::Painter(uint32_t maxInstances)
    : maxInstances_(std::min(maxInstances, kMaxInstancesPerFrame)), opacity_{1.0f} {}

void Painter::requireActive(const char* call) const {
  if (!active_) throw std::logic_error(std::string("Painter::") + call + " called outside begin()/end()");
}

void Painter::begin(uint32_t width, uint32_t height) {
  list_.width = width;
  list_.height = height;
  list_.sdf.clear();
  list_.tex.clear();
  list_.batches.clear();
  clip_.reset(IRect{0, 0, core::checkedCast<int32_t>(width), core::checkedCast<int32_t>(height)});
  opacity_.assign(1, 1.0f);
  culled_ = 0;
  rejected_ = 0;
  dropped_ = 0;
  active_ = true;
}

void Painter::end() {
  requireActive("end");
  active_ = false;
  if (clip_.depth() != 0 || opacity_.size() != 1) {
    throw std::logic_error("Painter::end with an unbalanced clip or opacity stack");
  }
}

PaintStats Painter::stats() const {
  PaintStats s;
  s.drawCalls = core::checkedCast<uint32_t>(list_.batches.size());
  s.sdfInstances = core::checkedCast<uint32_t>(list_.sdf.size());
  s.texInstances = core::checkedCast<uint32_t>(list_.tex.size());
  s.instances = s.sdfInstances + s.texInstances;
  s.culled = culled_;
  s.rejected = rejected_;
  s.dropped = dropped_;
  return s;
}

// A draw is visible when its pixel-rounded-out bounds meet the current clip; the clip becomes the
// batch scissor. Counts the draw as culled otherwise.
bool Painter::visible(const Rect& bounds, IRect* scissor) {
  const IRect& clip = clip_.current();
  const double x0 = std::floor(std::clamp<double>(bounds.x, -kMaxCoordinate, kMaxCoordinate));
  const double y0 = std::floor(std::clamp<double>(bounds.y, -kMaxCoordinate, kMaxCoordinate));
  const double x1 = std::ceil(std::clamp(static_cast<double>(bounds.x) + bounds.w, -kMaxCoordinate, kMaxCoordinate));
  const double y1 = std::ceil(std::clamp(static_cast<double>(bounds.y) + bounds.h, -kMaxCoordinate, kMaxCoordinate));
  const IRect box{static_cast<int32_t>(x0), static_cast<int32_t>(y0), static_cast<int32_t>(x1 - x0),
                  static_cast<int32_t>(y1 - y0)};
  if (clip.empty() || intersect(clip, box).empty()) {
    ++culled_;
    return false;
  }
  *scissor = clip;
  return true;
}

template <class Instance>
Instance* Painter::append(BatchKind kind, const IRect& scissor, uint64_t textureId) {
  if (list_.sdf.size() + list_.tex.size() >= maxInstances_) {
    ++dropped_;
    return nullptr;
  }
  constexpr bool isSdf = std::is_same_v<Instance, SdfInstance>;
  auto& storage = [this]() -> auto& {
    if constexpr (isSdf) return list_.sdf; else return list_.tex;
  }();
  const uint32_t index = core::checkedCast<uint32_t>(storage.size());
  if (list_.batches.empty() || list_.batches.back().kind != kind ||
      !(list_.batches.back().scissor == scissor) || list_.batches.back().textureId != textureId) {
    list_.batches.push_back(Batch{kind, scissor, textureId, index, 0});
  }
  ++list_.batches.back().count;
  storage.push_back(Instance{});
  return &storage.back();
}

void Painter::fillRect(const Rect& rect, const Color& color) { fillRoundedRect(rect, CornerRadii{}, color); }

void Painter::fillRoundedRect(const Rect& rect, const CornerRadii& radii, const Color& color) {
  requireActive("fillRoundedRect");
  if (!allFinite({rect.x, rect.y, rect.w, rect.h, radii.topLeft, radii.topRight, radii.bottomRight,
                  radii.bottomLeft}) || !finiteColor(color)) {
    ++rejected_;
    return;
  }
  const float alpha = unit(color.a) * opacity_.back();
  if (!(rect.w > 0.0f) || !(rect.h > 0.0f) || !(alpha > 0.0f)) {
    ++culled_;
    return;
  }
  const Rect box = clampRect(rect);
  IRect scissor;
  if (!visible(box, &scissor)) return;
  SdfInstance* slot = append<SdfInstance>(BatchKind::Sdf, scissor, 0);
  if (slot == nullptr) return;
  SdfInstance& inst = *slot;
  setRect(inst.rect, box);
  setRadii(inst.radii, reference::normalizeRadii(box, radii));
  setColor(inst.color, color, alpha);
  inst.params[0] = static_cast<float>(SdfKind::Fill);
}

void Painter::border(const Rect& rect, const CornerRadii& radii, float width, const Color& color,
                     bool inside) {
  requireActive("border");
  if (!allFinite({rect.x, rect.y, rect.w, rect.h, radii.topLeft, radii.topRight, radii.bottomRight,
                  radii.bottomLeft, width}) || !finiteColor(color)) {
    ++rejected_;
    return;
  }
  const float alpha = unit(color.a) * opacity_.back();
  if (!(rect.w > 0.0f) || !(rect.h > 0.0f) || !(width > 0.0f) || !(alpha > 0.0f)) {
    ++culled_;
    return;
  }
  Rect outer = clampRect(rect);
  CornerRadii outerRadii = reference::normalizeRadii(outer, radii);
  float w = width;
  if (inside) {
    w = std::min(w, std::min(outer.w, outer.h) * 0.5f);
  } else {
    outer = clampRect(inflate(outer, w));
    const auto grow = [w](float r) { return r > 0.0f ? r + w : 0.0f; };
    outerRadii = reference::normalizeRadii(
        outer, CornerRadii{grow(outerRadii.topLeft), grow(outerRadii.topRight), grow(outerRadii.bottomRight),
                           grow(outerRadii.bottomLeft)});
  }
  IRect scissor;
  if (!visible(outer, &scissor)) return;
  SdfInstance* slot = append<SdfInstance>(BatchKind::Sdf, scissor, 0);
  if (slot == nullptr) return;
  SdfInstance& inst = *slot;
  setRect(inst.rect, outer);
  setRadii(inst.radii, outerRadii);
  setColor(inst.color, color, alpha);
  inst.params[0] = static_cast<float>(SdfKind::Border);
  inst.params[1] = w;
}

void Painter::shadow(const Rect& rect, const CornerRadii& radii, const ShadowSpec& spec) {
  requireActive("shadow");
  if (!allFinite({rect.x, rect.y, rect.w, rect.h, radii.topLeft, radii.topRight, radii.bottomRight,
                  radii.bottomLeft, spec.offsetX, spec.offsetY, spec.blur, spec.spread}) ||
      !finiteColor(spec.color)) {
    ++rejected_;
    return;
  }
  const float alpha = unit(spec.color.a) * opacity_.back();
  if (!(rect.w > 0.0f) || !(rect.h > 0.0f) || !(alpha > 0.0f)) {
    ++culled_;
    return;
  }
  const Rect box = clampRect(rect);
  const CornerRadii boxRadii = reference::normalizeRadii(box, radii);
  // CSS: the shadow box is the border box moved by the offset and grown by the spread; a rounded
  // corner grows with it, a square corner stays square.
  const float spread = std::clamp(spec.spread, -kMaxBlur, kMaxBlur);
  const Rect moved{box.x + spec.offsetX, box.y + spec.offsetY, box.w, box.h};
  const Rect shadowBox = clampRect(inflate(moved, spread));
  if (!(shadowBox.w > 0.0f) || !(shadowBox.h > 0.0f)) {
    ++culled_;
    return;
  }
  const auto grow = [spread](float r) { return r > 0.0f ? std::max(r + spread, 0.0f) : 0.0f; };
  const CornerRadii shadowRadii = reference::normalizeRadii(
      shadowBox, CornerRadii{grow(boxRadii.topLeft), grow(boxRadii.topRight), grow(boxRadii.bottomRight),
                             grow(boxRadii.bottomLeft)});
  const float sigma = std::clamp(spec.blur, 0.0f, kMaxBlur) * 0.5f;
  IRect scissor;
  if (!visible(inflate(shadowBox, 3.0f * sigma + 1.0f), &scissor)) return;
  SdfInstance* slot = append<SdfInstance>(BatchKind::Sdf, scissor, 0);
  if (slot == nullptr) return;
  SdfInstance& inst = *slot;
  setRect(inst.rect, shadowBox);
  setRadii(inst.radii, shadowRadii);
  setColor(inst.color, spec.color, alpha);
  inst.params[0] = static_cast<float>(SdfKind::Shadow);
  inst.params[1] = sigma < kShadowGaussianSigmaFloor ? 0.0f : sigma;
  setRect(inst.knockRect, box);
  setRadii(inst.knockRadii, boxRadii);
}

void Painter::line(float x0, float y0, float x1, float y1, float width, const Color& color) {
  requireActive("line");
  if (!allFinite({x0, y0, x1, y1, width}) || !finiteColor(color)) {
    ++rejected_;
    return;
  }
  const float alpha = unit(color.a) * opacity_.back();
  const auto bound = [](float v) { return static_cast<float>(std::clamp<double>(v, -kMaxCoordinate, kMaxCoordinate)); };
  const float ax = bound(x0), ay = bound(y0), bx = bound(x1), by = bound(y1);
  if (!(width > 0.0f) || !(alpha > 0.0f) || (ax == bx && ay == by)) {
    ++culled_;
    return;
  }
  const float half = std::min(width, static_cast<float>(kMaxCoordinate)) * 0.5f + 1.0f;
  const Rect bounds{std::min(ax, bx) - half, std::min(ay, by) - half, std::fabs(bx - ax) + 2.0f * half,
                    std::fabs(by - ay) + 2.0f * half};
  IRect scissor;
  if (!visible(bounds, &scissor)) return;
  SdfInstance* slot = append<SdfInstance>(BatchKind::Sdf, scissor, 0);
  if (slot == nullptr) return;
  SdfInstance& inst = *slot;
  inst.rect[0] = ax;
  inst.rect[1] = ay;
  inst.rect[2] = bx;
  inst.rect[3] = by;
  setColor(inst.color, color, alpha);
  inst.params[0] = static_cast<float>(SdfKind::Line);
  inst.params[1] = std::min(width, static_cast<float>(kMaxCoordinate));
}

void Painter::drawTexture(const TextureRef& texture, const Rect& dst, const Rect& uv, const Color& tint) {
  requireActive("drawTexture");
  if (!texture.valid()) throw std::invalid_argument("Painter::drawTexture: invalid texture");
  if (!allFinite({dst.x, dst.y, dst.w, dst.h, uv.x, uv.y, uv.w, uv.h}) || !finiteColor(tint)) {
    ++rejected_;
    return;
  }
  const float alpha = unit(tint.a) * opacity_.back();
  if (!(dst.w > 0.0f) || !(dst.h > 0.0f) || !(alpha > 0.0f)) {
    ++culled_;
    return;
  }
  const Rect box = clampRect(dst);
  IRect scissor;
  if (!visible(box, &scissor)) return;
  // Clamping the destination crops it; the same part must be cut from the texture window, or an
  // oversized quad would show a stretched texture instead of a cropped one.
  const double uPerPx = static_cast<double>(uv.w) / static_cast<double>(dst.w);
  const double vPerPx = static_cast<double>(uv.h) / static_cast<double>(dst.h);
  const double u0 = static_cast<double>(uv.x) + (static_cast<double>(box.x) - static_cast<double>(dst.x)) * uPerPx;
  const double v0 = static_cast<double>(uv.y) + (static_cast<double>(box.y) - static_cast<double>(dst.y)) * vPerPx;
  TexInstance* slot = append<TexInstance>(BatchKind::Textured, scissor, texture.id);
  if (slot == nullptr) return;
  TexInstance& inst = *slot;
  setRect(inst.dst, box);
  inst.uv[0] = static_cast<float>(u0);
  inst.uv[1] = static_cast<float>(v0);
  inst.uv[2] = static_cast<float>(u0 + static_cast<double>(box.w) * uPerPx);
  inst.uv[3] = static_cast<float>(v0 + static_cast<double>(box.h) * vPerPx);
  setColor(inst.tint, tint, alpha);
  inst.params[0] = static_cast<float>(texture.kind == TextureKind::Coverage ? TexMode::Coverage : TexMode::Color);
}

void Painter::pushClip(const Rect& rect) {
  requireActive("pushClip");
  clip_.push(rect);
}

void Painter::popClip() {
  requireActive("popClip");
  clip_.pop();
}

void Painter::pushOpacity(float opacity) {
  requireActive("pushOpacity");
  const float clamped = std::isnan(opacity) ? 0.0f : unit(opacity);
  opacity_.push_back(opacity_.back() * clamped);
}

void Painter::popOpacity() {
  requireActive("popOpacity");
  if (opacity_.size() <= 1) throw std::logic_error("Painter::popOpacity without a matching push");
  opacity_.pop_back();
}

}  // namespace r1ui::render
