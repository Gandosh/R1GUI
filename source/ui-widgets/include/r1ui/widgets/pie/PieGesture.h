// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the geometry of a pie menu (which slot a direction points at, where a slot is drawn) and
//   PieGesture, the state machine of the right-mouse-hold gesture. Neither knows widgets, commands or
//   time sources: the caller feeds pointer samples and timestamps, the gesture says what to show and what
//   happened.
// Why: owner requirement 2026-10-10: hold the right button and release over a slot, OR flick quickly in a
//   slot's direction without pinpointing it; a quick click opens nothing and falls back to the normal
//   context menu; Escape or returning to the centre and releasing cancels. Keeping the rules in one pure
//   object makes every one of them testable with synthetic samples and an injected clock.
// Callers: PieTrigger (the widget glue), PieMenu (geometry for drawing), tests.
//
// Rules (all distances are logical pixels from the press point, "the origin"):
//   * Dead zone: within deadZone of the origin no slot is selected. Outside it the slot is the one whose
//     sector (centred on the slot's direction, 360/slotCount degrees wide) contains the direction of the
//     pointer; the distance beyond the dead zone does not matter (a flick need not reach the slot).
//   * Slot 0 points up and slots run clockwise at equal angles (4, 6 or 8 slots). Slots that are not
//     selectable (empty, command missing or disabled) are never highlighted and never executed.
//   * Draw: the pie becomes visible once the button has been held drawDelayMs (150) and not before; a
//     flick that is released earlier still selects by direction without ever drawing.
//   * Release: outside the dead zone over a selectable slot -> Execute(slot). Outside the dead zone over
//     a slot that is not selectable -> Cancel. Inside the dead zone: if the pointer never left it and the
//     button was held less than clickMaxMs (180) -> Fallback (a click: the host opens its normal context
//     menu); otherwise -> Cancel (the user went back to the centre, or held without choosing).
//   * Non-finite pointer samples are dropped. cancel() ends the gesture without an outcome.
// Time is a uint64_t millisecond clock supplied by the caller; a clock that runs backwards counts as zero
//   elapsed time.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace r1ui::widgets {

struct PieGestureConfig {
  uint64_t drawDelayMs = 150;
  uint64_t clickMaxMs = 180;
  double deadZone = 24.0;
};

// ---- geometry -----------------------------------------------------------------------------------

inline constexpr double kPieSlotRadius = 124.0;  // centre of the pie to the centre of a slot

// Direction of slot `index` in degrees clockwise from up (0 = up). Out-of-range inputs give 0.
double pieSlotAngle(int slotCount, int index);
// The slot a displacement from the origin points at; nullopt inside the dead zone, for non-finite input
// or an invalid slotCount.
std::optional<int> pieSlotForDirection(double dx, double dy, int slotCount, double deadZone);
// Centre of slot `index` relative to the centre of the pie (x right, y down), `radius` away.
struct PiePoint {
  double x = 0.0;
  double y = 0.0;
};
PiePoint pieSlotOffset(int slotCount, int index, double radius = kPieSlotRadius);

// ---- gesture ------------------------------------------------------------------------------------

enum class PieOutcomeKind : uint8_t { None, Execute, Cancel, Fallback };

struct PieOutcome {
  PieOutcomeKind kind = PieOutcomeKind::None;
  int slot = -1;  // Execute: the slot
};

class PieGesture {
 public:
  explicit PieGesture(PieGestureConfig config = {}) : config_(config) {}

  const PieGestureConfig& config() const { return config_; }
  void setConfig(const PieGestureConfig& config);

  bool active() const { return active_; }
  bool drawn() const { return drawn_; }
  int highlighted() const { return highlight_; }
  int slotCount() const { return slotCount_; }
  double originX() const { return originX_; }
  double originY() const { return originY_; }

  // Starts a gesture at (x, y). `selectable` has one flag per slot (its size is the slot count: 4, 6 or
  // 8). False (nothing started) for a non-finite origin, a bad slot count or a gesture already active.
  bool begin(double x, double y, uint64_t nowMs, std::vector<bool> selectable);
  // A pointer sample. Returns true when the highlight changed.
  bool move(double x, double y);
  // Time passed: becomes drawn once the delay has elapsed. Returns true when it just became drawn.
  bool tick(uint64_t nowMs);
  // The button went up at (x, y). Ends the gesture.
  PieOutcome release(double x, double y, uint64_t nowMs);
  // Ends the gesture without an outcome (Escape, capture lost, window deactivated).
  void cancel();

  // Milliseconds until the pie should be drawn (0 when due or not pending).
  uint64_t msUntilDraw(uint64_t nowMs) const;

 private:
  int slotAt(double x, double y) const;  // selectable slot or -1

  PieGestureConfig config_;
  bool active_ = false;
  bool drawn_ = false;
  bool leftDeadZone_ = false;
  int highlight_ = -1;
  int slotCount_ = 0;
  double originX_ = 0.0;
  double originY_ = 0.0;
  uint64_t startMs_ = 0;
  std::vector<bool> selectable_;
};

}  // namespace r1ui::widgets
