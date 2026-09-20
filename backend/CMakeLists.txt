cmake_minimum_required(VERSION 3.24)

project(
    GYMahjongBackend
    VERSION 0.1.0
    DESCRIPTION "C++ backend for the GY Mahjong online game"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GYMJ_BUILD_TESTS "Build backend tests" ON)

add_subdirectory(include/third_party)
add_subdirectory(src)
add_subdirectory(app)

if(GYMJ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
