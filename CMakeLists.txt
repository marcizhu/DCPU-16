cmake_minimum_required(VERSION 3.1)
project(DCPU16 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

#---------------------------------------------------------------------------------------
# set default build to release
#---------------------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
endif()

message(STATUS "Build type: " ${CMAKE_BUILD_TYPE})

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)

add_executable(dcpu src/main.cpp src/clock.cpp src/dcpu.cpp src/keyboard.cpp src/lem1802.cpp)

target_include_directories(dcpu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${SDL2_INCLUDE_DIRS})
target_link_libraries(dcpu PRIVATE ${SDL2_LIBRARIES})

#---------------------------------------------------------------------------------------
# Turn on compiler warnings
#---------------------------------------------------------------------------------------
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
	target_compile_options(dcpu PUBLIC -Wall -Wextra -Wconversion -pedantic)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
	target_compile_options(dcpu PUBLIC /W3)
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()