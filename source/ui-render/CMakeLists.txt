# ui-render: Vulkan renderer for the toolkit. Needs the Vulkan SDK (headers + loader); when it
# is not found the module and the preview are skipped with a warning rather than failing the
# whole configure, so the other modules still build and test.
find_package(Vulkan)
if(NOT Vulkan_FOUND)
  message(WARNING "Vulkan SDK not found (set VULKAN_SDK): ui-render and the preview are disabled")
  return()
endif()

add_library(ui-render STATIC vulkan/Renderer.cpp vulkan/GpuResources.cpp)
add_library(r1ui::render ALIAS ui-render)
target_include_directories(ui-render PUBLIC include)
target_compile_definitions(ui-render PRIVATE VK_USE_PLATFORM_WIN32_KHR)
target_link_libraries(ui-render PUBLIC r1ui::platform PRIVATE Vulkan::Vulkan r1ui_options)
