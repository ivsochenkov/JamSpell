cmake_minimum_required(VERSION 3.18)
project(jamspell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#set(CMAKE_CXX_FLAGS "-std=c++11 -fPIC -g")

find_package(GTest)

link_directories(${PROJECT_BINARY_DIR}/jamspell)
include_directories(${CMAKE_SOURCE_DIR})

OPTION (JS_TRACE OFF)
if(JS_TRACE)
    message(STATUS "Building with trace: Enabled")
    add_definitions(-DJS_TRACE)
endif() 

find_package (Threads REQUIRED)
set(Boost_USE_MULTITHREADED ON)
find_package (Boost CONFIG REQUIRED locale)

add_subdirectory(jamspell)
add_subdirectory(main)
add_subdirectory(contrib)
add_subdirectory(web_server)

if(GTest_FOUND)
    add_subdirectory(tests)
endif()
