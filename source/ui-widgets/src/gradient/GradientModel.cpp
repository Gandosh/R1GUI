// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GradientModel.h.
// Invariants: see the header; every mutator re-establishes the sorted order with a stable sort and
//   validates before touching the vector.
// Callers: the gradient editor and its tests.
#include "r1ui/widgets/gradient/GradientModel.h"

#include <algorithm>
#include <cmath>

namespace r1ui::widgets::gradient {

Gradient::Gradient() {
  stops_.push_back({0.0, {{212.0 / 255.0, 212.0 / 255.0, 212.0 / 255.0}, 1.0}, nextId_++});
  stops_.push_back({1.0, {{1.0, 1.0, 1.0}, 1.0}, nextId_++});
}

Gradient Gradient::fromStops(std::vector<std::pair<double, color::Rgba>> in) {
  Gradient g;
  g.stops_.clear();
  g.nextId_ = 1;
  if (in.size() > kMaxStops) in.resize(kMaxStops);
  for (const auto& [position, colour] : in) g.stops_.push_back({color::clamp01(position), color::sanitized(colour), g.nextId_++});
  while (g.stops_.size() < kMinStops) {
    const double position = g.stops_.empty() ? 0.0 : 1.0;
    const color::Rgba colour = g.stops_.empty() ? color::Rgba{{0, 0, 0}, 1.0} : g.stops_.back().color;
    g.stops_.push_back({position, colour, g.nextId_++});
  }
  g.sortStops();
  return g;
}

void Gradient::sortStops() {
  std::stable_sort(stops_.begin(), stops_.end(), [](const Stop& a, const Stop& b) { return a.position < b.position; });
}

size_t Gradient::indexOf(uint32_t id) const {
  for (size_t i = 0; i < stops_.size(); ++i) {
    if (stops_[i].id == id) return i;
  }
  return npos;
}

const Stop* Gradient::find(uint32_t id) const {
  const size_t i = indexOf(id);
  return i == npos ? nullptr : &stops_[i];
}

color::Rgba Gradient::evaluate(double tIn) const {
  const double t = color::clamp01(tIn);
  if (t <= stops_.front().position) return stops_.front().color;
  if (t >= stops_.back().position) return stops_.back().color;
  // The first stop strictly after t; the pair around t always exists here.
  size_t hi = 1;
  while (hi < stops_.size() && stops_[hi].position <= t) ++hi;
  if (hi >= stops_.size()) return stops_.back().color;
  const Stop& a = stops_[hi - 1];
  const Stop& b = stops_[hi];
  const double span = b.position - a.position;
  if (span <= 0.0) return b.color;
  return color::mix(a.color, b.color, (t - a.position) / span);
}

uint32_t Gradient::addStop(double position, std::optional<color::Rgba> colour) {
  if (std::isnan(position) || stops_.size() >= kMaxStops) return 0;
  const double p = color::clamp01(position);
  const color::Rgba c = colour ? color::sanitized(*colour) : evaluate(p);
  Stop stop{p, c, nextId_++};
  // After every stop at or before p, so a new stop on an existing one sits on top of it.
  auto it = std::upper_bound(stops_.begin(), stops_.end(), p, [](double v, const Stop& s) { return v < s.position; });
  stops_.insert(it, stop);
  return stop.id;
}

bool Gradient::removeStop(uint32_t id) {
  if (stops_.size() <= kMinStops) return false;
  const size_t i = indexOf(id);
  if (i == npos) return false;
  stops_.erase(stops_.begin() + static_cast<std::ptrdiff_t>(i));
  return true;
}

bool Gradient::moveStop(uint32_t id, double position) {
  if (std::isnan(position)) return false;
  const size_t i = indexOf(id);
  if (i == npos) return false;
  const double p = color::clamp01(position);
  if (stops_[i].position == p) return false;
  // Moving right puts the stop after equal ones, moving left before them: remove and re-insert.
  const bool right = p > stops_[i].position;
  Stop moved = stops_[i];
  moved.position = p;
  stops_.erase(stops_.begin() + static_cast<std::ptrdiff_t>(i));
  auto it = right ? std::upper_bound(stops_.begin(), stops_.end(), p, [](double v, const Stop& s) { return v < s.position; })
                  : std::lower_bound(stops_.begin(), stops_.end(), p, [](const Stop& s, double v) { return s.position < v; });
  stops_.insert(it, moved);
  return true;
}

bool Gradient::setStopColor(uint32_t id, const color::Rgba& colour) {
  const size_t i = indexOf(id);
  if (i == npos) return false;
  const color::Rgba c = color::sanitized(colour);
  if (stops_[i].color == c) return false;
  stops_[i].color = c;
  return true;
}

bool Gradient::setType(GradientType type) {
  if (type == type_) return false;
  type_ = type;
  return true;
}

bool Gradient::setAngle(double degrees) {
  if (!std::isfinite(degrees)) return false;
  const double a = color::wrapHue(degrees);
  if (a == angle_) return false;
  angle_ = a;
  return true;
}

bool Gradient::setCenter(double x, double y) {
  if (std::isnan(x) || std::isnan(y)) return false;
  const double cx = color::clamp01(x);
  const double cy = color::clamp01(y);
  if (cx == centerX_ && cy == centerY_) return false;
  centerX_ = cx;
  centerY_ = cy;
  return true;
}

}  // namespace r1ui::widgets::gradient
