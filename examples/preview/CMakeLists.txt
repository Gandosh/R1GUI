# Interactive preview application (owner requirement: launchable after every phase).
# Built from the real toolkit modules; grows cumulatively each phase.
if(NOT TARGET r1ui::render)
  return()
endif()

add_executable(r1gui-preview WIN32 main.cpp)
target_link_libraries(r1gui-preview PRIVATE r1ui::platform r1ui::render r1ui_options)
