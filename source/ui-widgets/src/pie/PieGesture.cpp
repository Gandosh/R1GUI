// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the pie geometry functions and PieGesture of PieGesture.h.
// Invariants: every function is total (non-finite input is dropped or answered with "no slot"); the
//   gesture never reports a highlight for a slot that is not selectable; after release() or cancel() the
//   gesture is inactive and holds no state of the finished interaction.
// Callers: PieTrigger, PieMenu, tests.
#include "r1ui/widgets/pie/PieGesture.h"

#include <algorithm>
#include <cmath>

namespace r1ui::widgets {

namespace {
constexpr double kPi = 3.14159265358979323846;
bool validSlotCount(int count) { return count == 4 || count == 6 || count == 8; }
}  // namespace

double pieSlotAngle(int slotCount, int index) {
  if (!validSlotCount(slotCount) || index < 0 || index >= slotCount) return 0.0;
  return 360.0 * static_cast<double>(index) / static_cast<double>(slotCount);
}

std::optional<int> pieSlotForDirection(double dx, double dy, int slotCount, double deadZone) {
  if (!validSlotCount(slotCount) || !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(deadZone)) return std::nullopt;
  // Compare squared lengths: hostile huge values overflow hypot() to infinity only beyond 1e154.
  if (dx * dx + dy * dy <= deadZone * deadZone) return std::nullopt;
  double angle = std::atan2(dx, -dy) * 180.0 / kPi;  // 0 = up, clockwise (y grows downwards)
  if (angle < 0.0) angle += 360.0;
  const double width = 360.0 / static_cast<double>(slotCount);
  const int slot = static_cast<int>(std::floor((angle + width * 0.5) / width)) % slotCount;
  return std::clamp(slot, 0, slotCount - 1);
}

PiePoint pieSlotOffset(int slotCount, int index, double radius) {
  const double angle = pieSlotAngle(slotCount, index) * kPi / 180.0;
  return {std::sin(angle) * radius, -std::cos(angle) * radius};
}

// ---- gesture ------------------------------------------------------------------------------------

void PieGesture::setConfig(const PieGestureConfig& config) {
  PieGestureConfig c = config;
  c.drawDelayMs = std::min<uint64_t>(c.drawDelayMs, 10000);
  c.clickMaxMs = std::min<uint64_t>(c.clickMaxMs, 10000);
  if (!std::isfinite(c.deadZone)) c.deadZone = PieGestureConfig{}.deadZone;
  c.deadZone = std::clamp(c.deadZone, 4.0, 200.0);
  config_ = c;
}

int PieGesture::slotAt(double x, double y) const {
  const std::optional<int> slot = pieSlotForDirection(x - originX_, y - originY_, slotCount_, config_.deadZone);
  if (!slot || !selectable_[static_cast<size_t>(*slot)]) return -1;
  return *slot;
}

bool PieGesture::begin(double x, double y, uint64_t nowMs, std::vector<bool> selectable) {
  if (active_ || !std::isfinite(x) || !std::isfinite(y) || selectable.size() > 8 || !validSlotCount(static_cast<int>(selectable.size()))) return false;
  active_ = true;
  drawn_ = false;
  leftDeadZone_ = false;
  highlight_ = -1;
  slotCount_ = static_cast<int>(selectable.size());
  selectable_ = std::move(selectable);
  originX_ = x;
  originY_ = y;
  startMs_ = nowMs;
  return true;
}

bool PieGesture::move(double x, double y) {
  if (!active_ || !std::isfinite(x) || !std::isfinite(y)) return false;
  const double dx = x - originX_;
  const double dy = y - originY_;
  if (dx * dx + dy * dy > config_.deadZone * config_.deadZone) leftDeadZone_ = true;
  const int slot = slotAt(x, y);
  const bool changed = slot != highlight_;
  highlight_ = slot;
  return changed;
}

bool PieGesture::tick(uint64_t nowMs) {
  if (!active_ || drawn_) return false;
  if (nowMs >= startMs_ && nowMs - startMs_ >= config_.drawDelayMs) {
    drawn_ = true;
    return true;
  }
  return false;
}

uint64_t PieGesture::msUntilDraw(uint64_t nowMs) const {
  if (!active_ || drawn_) return 0;
  const uint64_t elapsed = nowMs >= startMs_ ? nowMs - startMs_ : 0;
  return elapsed >= config_.drawDelayMs ? 0 : config_.drawDelayMs - elapsed;
}

PieOutcome PieGesture::release(double x, double y, uint64_t nowMs) {
  PieOutcome outcome;
  if (!active_) return outcome;
  // A non-finite release position counts as "no movement since the last sample".
  if (std::isfinite(x) && std::isfinite(y)) move(x, y);
  const uint64_t held = nowMs >= startMs_ ? nowMs - startMs_ : 0;
  const int slot = highlight_;
  const bool left = leftDeadZone_;
  const bool outside = std::isfinite(x) && std::isfinite(y) ? pieSlotForDirection(x - originX_, y - originY_, slotCount_, config_.deadZone).has_value() : false;
  cancel();
  if (slot >= 0) {
    outcome.kind = PieOutcomeKind::Execute;
    outcome.slot = slot;
  } else if (outside) {
    outcome.kind = PieOutcomeKind::Cancel;  // over a slot that cannot be chosen
  } else if (!left && held < config_.clickMaxMs) {
    outcome.kind = PieOutcomeKind::Fallback;
  } else {
    outcome.kind = PieOutcomeKind::Cancel;
  }
  return outcome;
}

void PieGesture::cancel() {
  active_ = false;
  drawn_ = false;
  leftDeadZone_ = false;
  highlight_ = -1;
  slotCount_ = 0;
  selectable_.clear();
}

}  // namespace r1ui::widgets
