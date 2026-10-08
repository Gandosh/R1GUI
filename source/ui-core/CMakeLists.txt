# ui-core: framework-neutral primitives shared by every toolkit module.
add_library(ui-core INTERFACE)
add_library(r1ui::core ALIAS ui-core)
target_include_directories(ui-core INTERFACE include)
target_compile_features(ui-core INTERFACE cxx_std_20)
target_link_libraries(ui-core INTERFACE r1ui_options)
