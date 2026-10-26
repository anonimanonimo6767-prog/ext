cmake_minimum_required(VERSION 3.20)
project(v67suite)
set(CMAKE_CXX_STANDARD 17)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)

add_executable(v67suite WIN32
    cheat.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(v67suite PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(v67suite PRIVATE
    d3d11 dxgi d3dcompiler dwmapi user32 gdi32 winhttp
)

target_compile_definitions(v67suite PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

target_compile_options(v67suite PRIVATE /O2 /W3)
