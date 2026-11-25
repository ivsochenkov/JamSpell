cmake_minimum_required(VERSION 3.18)
project(sivspell-jamspell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

link_directories(${PROJECT_BINARY_DIR}/jamspell)
include_directories(${CMAKE_SOURCE_DIR})

OPTION (JS_TRACE OFF)
if(JS_TRACE)
    message(STATUS "Building with trace: Enabled")
    add_definitions(-DJS_TRACE)
endif() 

add_subdirectory(jamspell)
add_subdirectory(main)
add_subdirectory(contrib)
add_subdirectory(web_server)

OPTION (ENABLE_SIVSPELL_TESTING OFF)
if(ENABLE_SIVSPELL_TESTING)
    add_subdirectory(tests)
endif()
