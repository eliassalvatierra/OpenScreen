cmake_minimum_required(VERSION 3.25)

project(OpenScreen VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

add_executable(openscreen
    src/main.cpp
    src/capture/CaptureManager.h
    src/capture/CaptureManager.cpp
)

target_link_libraries(openscreen
    PRIVATE
        Qt6::Widgets
        Qt6::DBus
        PkgConfig::PIPEWIRE
)

target_compile_options(openscreen PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
