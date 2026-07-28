# NORA C++ controller — SDL2 clone of scripts/controller.py
#
# BUILD (Windows, vcpkg manifest mode):
#   1. Install vcpkg once: https://github.com/microsoft/vcpkg
#   2. cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake
#   3. cmake --build build --config Release
#   4. build/Release/nora_controller.exe   (run from this folder, or pass --font)
#
# vcpkg.json in this folder pulls sdl2 + sdl2-ttf automatically the first
# time you configure -- no manual `vcpkg install` needed if manifest mode
# is on (it is, by default, once VCPKG_ROOT / the toolchain file is set).
#
# WiFi transport uses WinHTTP (ships with Windows, no extra dependency).
# Bluetooth serial isn't implemented in this port yet -- WiFi only for now.

cmake_minimum_required(VERSION 3.20)
project(nora_controller CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)

add_executable(nora_controller
    src/main.cpp
)

target_include_directories(nora_controller PRIVATE src)

if (WIN32)
    target_link_libraries(nora_controller PRIVATE winhttp)
endif()

target_link_libraries(nora_controller PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    SDL2::SDL2
    $<IF:$<TARGET_EXISTS:SDL2_ttf::SDL2_ttf>,SDL2_ttf::SDL2_ttf,SDL2_ttf::SDL2_ttf-static>
)
