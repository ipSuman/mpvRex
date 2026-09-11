cmake_minimum_required(VERSION 3.22)
project(REXPlayerLinux VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBMPV REQUIRED IMPORTED_TARGET mpv)

add_executable(rex-player
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
)

target_link_libraries(rex-player PRIVATE
    Qt6::Widgets
    PkgConfig::LIBMPV
)

target_compile_options(rex-player PRIVATE -fpermissive)

install(TARGETS rex-player RUNTIME DESTINATION bin)
