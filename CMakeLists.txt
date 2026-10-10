cmake_minimum_required(VERSION 3.22)

project(SheepdogDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(USE_WAYLAND OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(raylib)

add_executable(sheepdog-demo
    src/main.cpp
    src/game.cpp
    src/game.h
)

target_compile_options(sheepdog-demo PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-missing-field-initializers>
)

target_link_libraries(sheepdog-demo PRIVATE raylib)

target_link_options(sheepdog-demo PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-static;-static-libgcc;-static-libstdc++>
)
