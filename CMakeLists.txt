cmake_minimum_required(VERSION 3.16)
project(gpu-hud CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(TP ${CMAKE_SOURCE_DIR}/third_party)

# Headers fetched by scripts/bootstrap-headers.sh when the -dev packages are not installed.
if(EXISTS ${TP}/sysroot/usr/include/X11/Xlib.h)
  include_directories(SYSTEM ${TP}/sysroot/usr/include)
  list(APPEND CMAKE_PREFIX_PATH ${TP}/sysroot/usr)
  set(CMAKE_REQUIRED_INCLUDES ${TP}/sysroot/usr/include)
endif()

# GLFW (X11 backend; it dlopens libX11/libGL at runtime).
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${TP}/glfw EXCLUDE_FROM_ALL)
if(EXISTS ${TP}/sysroot/usr/include/X11/Xlib.h)
  target_include_directories(glfw PRIVATE ${TP}/sysroot/usr/include)
endif()

set(IMGUI ${TP}/imgui)
add_library(imgui STATIC
  ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
  ${IMGUI}/backends/imgui_impl_glfw.cpp ${IMGUI}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI})
target_link_libraries(imgui PUBLIC glfw)

add_executable(gpu-hud src/main.cpp src/metrics.cpp src/sysinfo.cpp src/i18n.cpp)
target_include_directories(gpu-hud PRIVATE ${TP}/stb)
target_link_libraries(gpu-hud PRIVATE imgui glfw ${CMAKE_DL_LIBS} pthread)

install(TARGETS gpu-hud RUNTIME DESTINATION bin)
install(FILES gpu-hud.desktop DESTINATION share/applications)
