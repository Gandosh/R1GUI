# ui-platform: thin OS layer (windows, input, DPI, clipboard, monitors). Win32 only for now;
# other backends are added behind the same public headers after the first release.
# common/ holds OS-independent logic (UTF conversion, DPI math, hit-testing, monitor clamping)
# so it is unit-testable without a desktop; win32/ holds everything that touches the OS.
add_library(ui-platform STATIC
  common/ChromeHitTest.cpp
  common/Dpi.cpp
  common/MonitorClamp.cpp
  common/Utf.cpp
  win32/ClipboardWin32.cpp
  win32/MonitorsWin32.cpp
  win32/Window.cpp
  win32/WindowChrome.cpp
  win32/WindowProc.cpp)
add_library(r1ui::platform ALIAS ui-platform)
target_include_directories(ui-platform PUBLIC include)
target_link_libraries(ui-platform PUBLIC r1ui::core PRIVATE r1ui_options user32 dwmapi shell32 shcore)
