cmake_minimum_required(VERSION 3.20)
project(VideoFrameExtractor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(imgui CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(Stb REQUIRED)

# vcpkg installed headers (includes imgui, ffmpeg, etc.)
set(VCPKG_INSTALLED_INCLUDE "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include")

# Source files
set(SOURCES
    src/main.cpp
    src/app.cpp
    src/decoder.cpp
    src/frame_writer.cpp
    src/imgui_impl_dx11.cpp
    src/imgui_impl_win32.cpp
)

set(HEADERS
    src/app.h
    src/decoder.h
    src/frame_writer.h
)

# Executable (WIN32 = no console window)
add_executable(video_frame_extractor WIN32 ${SOURCES} ${HEADERS})

target_include_directories(video_frame_extractor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${VCPKG_INSTALLED_INCLUDE}
    ${FFMPEG_INCLUDE_DIRS}
    ${Stb_INCLUDE_DIR}
)

target_compile_options(video_frame_extractor PRIVATE
    /utf-8   # MSVC: treat source as UTF-8 (fix C4819 for Chinese comments)
)

target_link_libraries(video_frame_extractor PRIVATE
    imgui::imgui
    ${FFMPEG_LIBRARIES}
    d3d11
    dxgi
    dxguid
    d3dcompiler
)

# Windows extras
if(WIN32)
    target_link_libraries(video_frame_extractor PRIVATE
        comdlg32
        ole32
        dwmapi
    )
endif()
