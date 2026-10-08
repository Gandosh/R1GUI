# ui-platform: thin OS layer (windows, input, DPI). Win32 only for now; other backends
# are added behind the same public headers after the first release.
add_library(ui-platform STATIC win32/Window.cpp)
add_library(r1ui::platform ALIAS ui-platform)
target_include_directories(ui-platform PUBLIC include)
target_link_libraries(ui-platform PUBLIC r1ui::core PRIVATE r1ui_options)
