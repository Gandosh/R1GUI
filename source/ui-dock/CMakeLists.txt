# ui-dock: headless dock model (tab stacks, splits, floating areas), its geometry, drop-zone
# hit testing and layout persistence. No Vulkan or OS types; depends only on ui-core.
add_library(ui-dock STATIC
  src/DockTree.cpp
  src/DockOps.cpp
  src/DockGeometry.cpp
  src/DockSerialize.cpp)
add_library(r1ui::dock ALIAS ui-dock)
target_include_directories(ui-dock PUBLIC include PRIVATE src)
target_link_libraries(ui-dock PUBLIC r1ui::core PRIVATE r1ui_options)
