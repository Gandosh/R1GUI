# ui-core: framework-neutral primitives shared by every toolkit module.
# r1ui_options is PUBLIC so dependants (and their tests) inherit the warning policy.
add_library(ui-core STATIC src/Json.cpp)
add_library(r1ui::core ALIAS ui-core)
target_include_directories(ui-core PUBLIC include)
target_compile_features(ui-core PUBLIC cxx_std_20)
target_link_libraries(ui-core PUBLIC r1ui_options)
