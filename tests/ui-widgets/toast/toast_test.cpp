// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: behaviour tests of the toast manager without a GPU: placement at the top centre 8 px from the
//   edge, the measured box (28 px high, text + 32 wide), stacking with an 8 px gap, tone lifetimes
//   (3 s, 5 s, 10 s), fade-out before removal when animations run, pause on hover and resume on
//   leave, the error toast's copy and close buttons, the visible-toast limit, callbacks that run
//   once, and hostile input (empty and invalid text, a megabyte of text, never-expiring toasts,
//   unknown ids, a toast destroyed from outside, showing from inside a close callback, a zero-size
//   window, the manager destroyed while toasts live).
// Callers: CTest (label fast).
#include "TestSupport.h"
#include "r1ui/widgets/toast/Toast.h"
#include "r1ui/widgets/toast/ToastParts.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;

ToastSpec spec(std::string text, ToastTone tone = ToastTone::Default) {
  ToastSpec s;
  s.text = std::move(text);
  s.tone = tone;
  return s;
}

void testPlacementAndStacking() {
  r1test::TestUi t(800, 600);
  ToastManager toasts(t.ui);
  const ToastId a = toasts.show(spec("Copied as node ID"));
  t.layout();
  R1_EXPECT(a != 0 && toasts.count() == 1);
  const auto ra = t.ui.absRect(toasts.widgetOf(a));
  R1_EXPECT(ra.y == 8 && ra.h == 28);
  R1_EXPECT(std::abs((ra.x + ra.w / 2) - 400) <= 1);  // centred
  // Text + icon (12) + gap (6) + padding (2 x 10).
  const double textWidth = t.ui.text().measure("Copied as node ID", 12.0f);
  R1_EXPECT_NEAR(static_cast<double>(ra.w), textWidth + 12 + 6 + 20, 1.0);
  // The next toast stacks below with an 8 px gap.
  const ToastId b = toasts.show(spec("Second"));
  t.layout();
  const auto rb = t.ui.absRect(toasts.widgetOf(b));
  R1_EXPECT(rb.y == ra.bottom() + 8);
  // The error toast with copy and close buttons is 30 px high.
  const ToastId e = toasts.show(spec("Failed to open file: unexpected token", ToastTone::Error));
  t.layout();
  R1_EXPECT(t.ui.absRect(toasts.widgetOf(e)).h == 30);
  // Overlay hosts do not move toasts: the toast layer is above every popup.
  R1_EXPECT(toasts.count() == 3);
}

void testLifetimes() {
  r1test::TestUi t(800, 600);
  ToastManager toasts(t.ui);
  int closed = 0;
  t.ui.setTime(1000);
  ToastSpec d = spec("default");
  d.onClosed = [&] { ++closed; };
  toasts.show(d);
  const ToastId w = toasts.show(spec("warning", ToastTone::Warning));
  const ToastId e = toasts.show(spec("error", ToastTone::Error));
  t.layout();
  t.ui.setTime(3999);
  t.ui.tick();
  R1_EXPECT(toasts.count() == 3 && closed == 0);
  t.ui.setTime(4000);  // 3 s after it was shown
  R1_EXPECT(t.ui.tick());
  t.layout();
  R1_EXPECT(toasts.count() == 2 && closed == 1);
  t.ui.setTime(5999);
  t.ui.tick();
  R1_EXPECT(toasts.count() == 2);
  t.ui.setTime(6000);
  t.ui.tick();
  R1_EXPECT(toasts.count() == 1 && toasts.widgetOf(w) == WidgetId{} && toasts.widgetOf(e).valid());
  t.ui.setTime(10999);
  t.ui.tick();
  R1_EXPECT(toasts.count() == 1);
  t.ui.setTime(11000);
  t.ui.tick();
  R1_EXPECT(toasts.count() == 0 && closed == 1);
  // A toast that never expires stays; the tick has nothing scheduled afterwards.
  ToastSpec forever = spec("stay");
  forever.durationMs = kToastNeverExpires;
  toasts.show(forever);
  t.ui.setTime(1000000);
  t.ui.tick();
  R1_EXPECT(toasts.count() == 1 && !t.ui.msUntilTick().has_value());
  toasts.dismissAll();
  R1_EXPECT(toasts.count() == 0);
}

void testFadeOutAndHover() {
  r1test::TestUi t(800, 600);
  t.ui.setAnimationsEnabled(true);
  t.ui.setFrameLoopRunning(true);
  ToastManager toasts(t.ui);
  t.ui.setTime(1000);
  const ToastId id = toasts.show(spec("hover me"));
  t.layout();
  const auto r = t.ui.absRect(toasts.widgetOf(id));
  // Hover pauses the countdown: nothing happens at 3 s while the pointer is inside.
  t.ui.setTime(2000);
  t.ui.pointerMove(r.x + 10, r.y + 10);
  t.ui.setTime(4500);
  t.ui.tick();
  R1_EXPECT(toasts.count() == 1);
  // Leaving resumes with the remaining time (about 2 s left of the 3 s), not a fresh 3 s.
  t.ui.setTime(5000);
  t.ui.pointerMove(700, 500);
  t.ui.setTime(6900);
  t.ui.tick();
  R1_EXPECT(toasts.count() == 1);
  t.ui.setTime(7100);
  t.ui.tick();
  // Expired: the fade-out has started, the toast is removed when it ran out (150 ms).
  R1_EXPECT(toasts.count() == 1 && t.ui.objectAs<ToastWidget>(toasts.widgetOf(id))->closing());
  t.ui.setTime(7300);
  t.ui.tick();
  R1_EXPECT(toasts.count() == 0);
}

void testButtons() {
  r1test::TestUi base(800, 600);
  std::string clipboard;
  UiContextOptions options;
  options.host.writeClipboard = [&](std::string_view text) { clipboard = std::string(text); };
  UiContext ui(base.services, options);
  ui.setViewport(800, 600, 1.0f);
  ui.setAnimationsEnabled(false);
  ToastManager toasts(ui);
  int closed = 0;
  ToastSpec s = spec("Failed to open file: unexpected token", ToastTone::Error);
  s.onClosed = [&] { ++closed; };
  const ToastId id = toasts.show(s);
  ui.frame();
  const ToastWidget* w = ui.objectAs<ToastWidget>(toasts.widgetOf(id));
  R1_EXPECT(w != nullptr && w->hasCopy() && w->hasClose());
  const auto box = ui.absRect(w->id());
  const auto copy = w->buttonRect(0);
  const auto close = w->buttonRect(1);
  R1_EXPECT(copy.w > 0 && close.w > 0 && copy.x < close.x);
  // The copy button copies the text and leaves the toast; the close button removes it.
  ui.pointerMove(box.x + copy.x + 6, box.y + copy.y + 8);
  R1_EXPECT(ui.cursor() == Cursor::Pointer);
  ui.pointerDown(box.x + copy.x + 6, box.y + copy.y + 8);
  ui.pointerUp(box.x + copy.x + 6, box.y + copy.y + 8);
  R1_EXPECT(clipboard == "Failed to open file: unexpected token" && toasts.count() == 1);
  ui.pointerMove(box.x + close.x + 6, box.y + close.y + 8);
  ui.pointerDown(box.x + close.x + 6, box.y + close.y + 8);
  ui.pointerUp(box.x + close.x + 6, box.y + close.y + 8);
  R1_EXPECT(toasts.count() == 0 && closed == 1);
  // Default toasts have no buttons.
  const ToastId plain = toasts.show(spec("plain"));
  ui.frame();
  const ToastWidget* p = ui.objectAs<ToastWidget>(toasts.widgetOf(plain));
  R1_EXPECT(!p->hasCopy() && !p->hasClose() && p->buttonRect(1).w == 0);
}

void testLimitAndHostile() {
  r1test::TestUi t(800, 600);
  ToastManager toasts(t.ui);
  toasts.setMaxVisible(3);
  int closed = 0;
  for (int i = 0; i < 6; ++i) {
    ToastSpec s = spec("toast " + std::to_string(i));
    s.onClosed = [&] { ++closed; };
    toasts.show(s);
  }
  R1_EXPECT(toasts.count() == 3 && closed == 3);
  toasts.dismissAll();
  R1_EXPECT(toasts.count() == 0);
  // Empty text, invalid UTF-8, a megabyte, unknown ids.
  R1_EXPECT(toasts.show(spec("")) == 0);
  const ToastId bad = toasts.show(spec(std::string("bad \xFF\xFE\xC0\xAF text")));
  const ToastId huge = toasts.show(spec(std::string(1 << 20, 'w')));
  t.layout();
  R1_EXPECT(bad != 0 && huge != 0);
  R1_EXPECT(t.ui.absRect(toasts.widgetOf(huge)).w <= 384);
  R1_EXPECT(t.ui.absRect(toasts.widgetOf(huge)).h <= 12 * 16 + 12);
  R1_EXPECT(!toasts.dismiss(424242));
  // A toast destroyed from outside: later dismissal and timers are harmless.
  const ToastId outside = toasts.show(spec("destroyed"));
  t.ui.destroy(toasts.widgetOf(outside));
  R1_EXPECT(toasts.dismiss(outside));
  t.ui.setTime(60000);
  t.ui.tick();
  t.layout();
  // Showing a toast from inside a close callback.
  ToastSpec chain = spec("first");
  chain.onClosed = [&] { toasts.show(spec("second")); };
  toasts.dismissAll();
  const ToastId first = toasts.show(chain);
  toasts.dismiss(first);
  R1_EXPECT(toasts.count() == 1);
  // The manager handle destroyed while its toasts live: they still run out their timers.
  {
    ToastManager temp(t.ui);
    temp.show(spec("orphan"));
  }
  t.ui.setTime(120000);
  t.ui.tick();
  t.ui.tick();
  t.layout();
  R1_EXPECT(t.ui.overlays().count() == 0);
  // A zero-size window.
  r1test::TestUi tiny(0, 0);
  ToastManager z(tiny.ui);
  z.show(spec("tiny"));
  tiny.layout();
  z.dismissAll();
}

}  // namespace

int main() {
  testPlacementAndStacking();
  testLifetimes();
  testFadeOutAndHover();
  testButtons();
  testLimitAndHostile();
  return r1test::finish();
}
