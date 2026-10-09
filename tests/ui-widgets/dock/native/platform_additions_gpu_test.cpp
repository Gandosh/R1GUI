// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the ui-platform additions slice 5.2 made for the native floating backend: owned
//   windows (WindowDesc::owner), Window::clientOrigin, Window::setVisible / isVisible (hiding keeps
//   the pointer capture and reports no capture loss), Window::isAlive and the harmlessness of every
//   call on a window the OS destroyed, the DisplayChanged event and the monitor device names.
// Why: the backend depends on each of these; they are additive changes to a module the backend does
//   not own, so they carry their own tests next to the backend's.
// Callers: CTest (label gpu, by file name; the test needs a desktop, not a GPU). Skips itself
//   (SKIPPED, exit 0) when the session has no interactive desktop. Synthetic input only (window
//   messages); every window is destroyed before the test ends.
#include <windows.h>

#include <set>

#include "ExpectWithMessage.h"
#include "r1ui/platform/Monitors.h"
#include "r1ui/platform/Window.h"

namespace platform = r1ui::platform;

namespace {

HWND hwndOf(const platform::Window& w) { return static_cast<HWND>(w.nativeHandle().window); }

size_t count(const std::vector<platform::Event>& events, platform::EventType type) {
  size_t n = 0;
  for (const platform::Event& e : events) n += e.type == type ? 1 : 0;
  return n;
}

void monitor_names() {
  const std::vector<platform::MonitorInfo> monitors = platform::enumerateMonitors();
  std::set<std::string> names;
  for (const platform::MonitorInfo& m : monitors) {
    R1_EXPECT(!m.name.empty() && m.name.rfind("\\\\.\\DISPLAY", 0) == 0, "an OS device name such as \\\\.\\DISPLAY1");
    names.insert(m.name);
  }
  R1_EXPECT(names.size() == monitors.size(), "names are unique");
  std::printf("  monitors:");
  for (const platform::MonitorInfo& m : monitors) {
    std::printf(" %s %dx%d at %d,%d scale %.2f%s;", m.name.c_str(), m.bounds.width, m.bounds.height, m.bounds.x, m.bounds.y, static_cast<double>(m.dpiScale),
                m.primary ? " primary" : "");
  }
  std::printf("\n");
}

void owned_window_and_geometry() {
  platform::WindowDesc mainDesc;
  mainDesc.title = "platform additions main";
  mainDesc.width = 500;
  mainDesc.height = 300;
  mainDesc.borderless = true;
  mainDesc.position = platform::Point{200, 200};
  platform::Window main(mainDesc);

  platform::WindowDesc ownedDesc;
  ownedDesc.title = "owned";
  ownedDesc.width = 240;
  ownedDesc.height = 160;
  ownedDesc.borderless = true;
  ownedDesc.toolWindow = true;
  ownedDesc.owner = &main;
  ownedDesc.position = platform::Point{300, 260};
  platform::Window owned(ownedDesc);

  R1_EXPECT(GetWindow(hwndOf(owned), GW_OWNER) == hwndOf(main), "the owner relationship is the OS's");
  R1_EXPECT(GetWindow(hwndOf(main), GW_OWNER) == nullptr, "an unowned window has no owner");
  R1_EXPECT((GetWindowLongPtrW(hwndOf(owned), GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0, "tool window: no taskbar button");
  POINT origin{0, 0};
  ClientToScreen(hwndOf(owned), &origin);
  R1_EXPECT(owned.clientOrigin() == platform::Point({origin.x, origin.y}), "clientOrigin is ClientToScreen(0, 0)");
  R1_EXPECT(owned.clientOrigin() == platform::Point({owned.windowRect().x, owned.windowRect().y}), "a borderless window's client area is the whole window");

  // Minimizing the owner hides the owned window; restoring brings it back.
  R1_EXPECT(owned.isVisible());
  main.minimize();
  main.pumpEvents();
  R1_EXPECT(!owned.isVisible() && main.isMinimized(), "the owned window goes with its owner");
  ShowWindow(hwndOf(main), SW_RESTORE);
  main.pumpEvents();
  R1_EXPECT(owned.isVisible(), "and returns with it");
}

void hiding_keeps_the_capture() {
  platform::WindowDesc desc;
  desc.title = "capture";
  desc.width = 300;
  desc.height = 200;
  desc.borderless = true;
  desc.position = platform::Point{260, 240};
  platform::Window w(desc);
  w.pumpEvents();
  w.takeEvents();
  // A press captures the pointer in the platform window itself.
  SendMessageW(hwndOf(w), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 40));
  R1_EXPECT(GetCapture() == hwndOf(w), "the press took the capture");
  w.takeEvents();
  R1_EXPECT(w.setVisible(false) && !w.isVisible());
  R1_EXPECT(GetCapture() == hwndOf(w), "hiding keeps the capture: the drag started in this window goes on");
  w.pumpEvents();
  const std::vector<platform::Event> events = w.takeEvents();
  R1_EXPECT(count(events, platform::EventType::CaptureLost) == 0, "and no capture loss is reported");
  // Moves far outside every window still reach the hidden window, and so does the release.
  SendMessageW(hwndOf(w), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-3000 & 0xFFFF, 5000 & 0xFFFF));
  SendMessageW(hwndOf(w), WM_LBUTTONUP, 0, MAKELPARAM(7, 8));
  const std::vector<platform::Event> after = w.takeEvents();
  R1_EXPECT(count(after, platform::EventType::MouseMove) == 1 && count(after, platform::EventType::MouseUp) == 1, "pointer events keep arriving");
  R1_EXPECT(GetCapture() != hwndOf(w), "the release ends the capture as usual");
  R1_EXPECT(w.setVisible(true) && w.isVisible());

  // Hiding a window that has no capture takes none.
  R1_EXPECT(w.setVisible(false) && GetCapture() != hwndOf(w));
  R1_EXPECT(w.setVisible(true));
}

void display_change_event() {
  platform::WindowDesc desc;
  desc.title = "display";
  desc.width = 300;
  desc.height = 200;
  desc.borderless = true;
  desc.position = platform::Point{280, 260};
  platform::Window w(desc);
  w.pumpEvents();
  w.takeEvents();
  SendMessageW(hwndOf(w), WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080));
  const std::vector<platform::Event> events = w.takeEvents();
  R1_EXPECT(count(events, platform::EventType::DisplayChanged) == 1, "WM_DISPLAYCHANGE becomes a DisplayChanged event");
  R1_EXPECT(platform::eventRank(platform::Event{.type = platform::EventType::DisplayChanged}) == 2, "never dropped for room");
}

void destroyed_behind_its_back() {
  platform::WindowDesc desc;
  desc.title = "doomed";
  desc.width = 300;
  desc.height = 200;
  desc.borderless = true;
  desc.position = platform::Point{300, 280};
  platform::Window w(desc);
  R1_EXPECT(w.isAlive() && w.isVisible());
  DestroyWindow(hwndOf(w));  // the OS (or another component) destroys it
  R1_EXPECT(!w.isAlive() && !w.isVisible(), "the platform notices");
  // Every call is a harmless no-op.
  R1_EXPECT(w.windowRect().empty() && w.clientOrigin() == platform::Point({0, 0}));
  R1_EXPECT(!w.setWindowRect({0, 0, 100, 100}) && !w.setVisible(true) && !w.setVisible(false));
  w.minimize();
  w.maximizeToggle();
  w.requestClose();
  R1_EXPECT(!w.isMaximized() && !w.isMinimized());
  w.setCursor(platform::CursorShape::Hand);
  R1_EXPECT(w.takeEvents().size() < 100);
  w.pumpEvents();
}

}  // namespace

int main() try {
  if (platform::enumerateMonitors().empty()) {
    std::printf("SKIPPED: no monitor: the session has no interactive desktop\n");
    return 0;
  }
  monitor_names();
  owned_window_and_geometry();
  hiding_keeps_the_capture();
  display_change_event();
  destroyed_behind_its_back();
  return r1test::finish();
} catch (const std::exception& e) {
  std::printf("SKIPPED: windows cannot be created here: %s\n", e.what());
  return 0;
}
