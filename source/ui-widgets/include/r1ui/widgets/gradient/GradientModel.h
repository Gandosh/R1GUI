// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the value model of a gradient fill: its type (linear, radial, angular), geometry (angle and
//   centre), and the ordered list of colour stops with stable ids, plus the operations the editor
//   needs (evaluate, add a stop that keeps the look, move, remove, recolour) with their validation.
// Why: the gradient editor edits one value through the bar, the stop list and the colour picker;
//   keeping the rules (sorted order, at least two stops, positions in 0..1, no NaN, bounded count)
//   in a pure module makes them testable without a UI and keeps hostile input away from the widgets.
// Callers: GradientEditor, GradientBar, the stop rows, tests, hosts that store the gradient.
//   Calls: ColorModel (colour mixing).
// Invariants: stops() is always sorted by position (ties keep their relative order), has between
//   kMinStops and kMaxStops entries, every position is in [0, 1] and every colour is sanitised; ids
//   are unique, non-zero and never reused within one Gradient (so a selection by id survives moves
//   and re-sorting). Mutators return whether anything changed and leave the gradient untouched on a
//   rejected argument (NaN, unknown id, removing the last two stops).
// Evaluation: positions before the first stop take its colour, after the last stop the last colour;
//   between stops colours mix in premultiplied sRGB (what CSS gradients do); at a position shared by
//   several stops (a hard edge) the later stop wins.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "r1ui/widgets/colorpicker/ColorModel.h"

namespace r1ui::widgets::gradient {

enum class GradientType : uint8_t { Linear, Radial, Angular };

struct Stop {
  double position = 0.0;  // 0..1
  color::Rgba color;
  uint32_t id = 0;
  friend bool operator==(const Stop&, const Stop&) = default;
};

inline constexpr size_t kMinStops = 2;
inline constexpr size_t kMaxStops = 64;

class Gradient {
 public:
  // The default fill of the app: grey D4D4D4 to white.
  Gradient();
  // Replaces the stops (validated: sanitised, sorted, cut to kMaxStops, padded to two stops). Ids are
  // reassigned in the given order.
  static Gradient fromStops(std::vector<std::pair<double, color::Rgba>> stops);

  GradientType type() const { return type_; }
  double angleDegrees() const { return angle_; }
  double centerX() const { return centerX_; }
  double centerY() const { return centerY_; }
  const std::vector<Stop>& stops() const { return stops_; }
  size_t size() const { return stops_.size(); }

  // Index of the stop with `id`, or npos.
  static constexpr size_t npos = static_cast<size_t>(-1);
  size_t indexOf(uint32_t id) const;
  const Stop* find(uint32_t id) const;

  color::Rgba evaluate(double t) const;

  // Adds a stop at `position` with `colour` (default: the gradient's colour there, so the look does
  // not change). Returns its id, or 0 when full or `position` is NaN.
  uint32_t addStop(double position, std::optional<color::Rgba> colour = std::nullopt);
  // Fails (false) for an unknown id or when only kMinStops remain.
  bool removeStop(uint32_t id);
  bool moveStop(uint32_t id, double position);
  bool setStopColor(uint32_t id, const color::Rgba& colour);

  bool setType(GradientType type);
  bool setAngle(double degrees);                // wraps into [0, 360)
  bool setCenter(double x, double y);           // clamped to 0..1

  friend bool operator==(const Gradient&, const Gradient&) = default;

 private:
  void sortStops();

  GradientType type_ = GradientType::Linear;
  double angle_ = 90.0;   // CSS-like: 90 = left to right
  double centerX_ = 0.5;
  double centerY_ = 0.5;
  std::vector<Stop> stops_;
  uint32_t nextId_ = 1;
};

}  // namespace r1ui::widgets::gradient
