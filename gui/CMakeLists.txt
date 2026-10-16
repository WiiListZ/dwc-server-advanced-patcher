cmake_minimum_required(VERSION 3.20)
project(dwc-patcher-gui LANGUAGES C CXX)

# Desktop GUI that sets up and runs the patcher build (make.sh) and installs
# the result into a dwc-server-advanced server. Dear ImGui and GLFW are
# downloaded at configure time.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  # Static runtime: the .exe runs without the Visual C++ redistributable.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
  URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
  URL_HASH SHA256=c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8.tar.gz
  URL_HASH SHA256=db3a2e02bfd6c269adf0968950573053d002f40bdfb9ef2e4a90bce804b0f286
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(glfw imgui)

find_package(OpenGL REQUIRED)

add_executable(dwc-patcher-gui
  src/main.cpp
  src/app.cpp
  src/process.cpp
  src/platform.cpp
  src/source_install.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(dwc-patcher-gui PRIVATE src ${imgui_SOURCE_DIR})
target_link_libraries(dwc-patcher-gui PRIVATE glfw OpenGL::GL)

if(WIN32)
  target_link_libraries(dwc-patcher-gui PRIVATE ws2_32)
  enable_language(RC)
  target_sources(dwc-patcher-gui PRIVATE assets/logo.rc)
  # A window application: no console window next to the GUI.
  set_target_properties(dwc-patcher-gui PROPERTIES WIN32_EXECUTABLE TRUE)
  if(MSVC)
    target_link_options(dwc-patcher-gui PRIVATE /ENTRY:mainCRTStartup)
    target_compile_options(dwc-patcher-gui PRIVATE /utf-8 /W3)
    target_compile_definitions(dwc-patcher-gui PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  target_compile_definitions(dwc-patcher-gui PRIVATE UNICODE _UNICODE)
endif()
if(UNIX AND NOT APPLE)
  find_package(Threads REQUIRED)
  target_link_libraries(dwc-patcher-gui PRIVATE Threads::Threads)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(source-install-test
    tests/source_install_test.cpp
    src/source_install.cpp
    src/process.cpp
    src/platform.cpp)
  target_include_directories(source-install-test PRIVATE src)
  if(MSVC)
    target_compile_options(source-install-test PRIVATE /utf-8)
    target_compile_definitions(source-install-test PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
  endif()
  if(UNIX AND NOT APPLE)
    target_link_libraries(source-install-test PRIVATE Threads::Threads)
  endif()
  add_test(NAME source-install COMMAND source-install-test)
endif()
