cmake_minimum_required(VERSION 3.13)
project(compiler)

set(CMAKE_CXX_STANDARD 23)

# Default build type: Release when not specified (single-config generators)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type specified. Defaulting to Release.")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

set(FB_EXT ".cpp")
add_compile_options(-Wall -Wno-register)

# options about libraries and includes
set(LIB_DIR "$ENV{CDE_LIBRARY_PATH}/native" CACHE STRING "directory of libraries")
set(INC_DIR "$ENV{CDE_INCLUDE_PATH}" CACHE STRING "directory of includes")
message(STATUS "Library directory: ${LIB_DIR}")
message(STATUS "Include directory: ${INC_DIR}")

# project link directories
link_directories(${LIB_DIR})

# project include directories
include_directories(src)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${INC_DIR})

# all of C/C++ source files
file(GLOB_RECURSE C_SOURCES CONFIGURE_DEPENDS "src/*.c")
file(GLOB_RECURSE _SOURCES CONFIGURE_DEPENDS "src/*.cpp")
set(ALL_SOURCES ${C_SOURCES} ${_SOURCES})

# Split out main so we can build a reusable static library for tests.
set(MAIN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
list(REMOVE_ITEM ALL_SOURCES ${MAIN_SRC})

# core static library used by the compiler and tests
add_library(core STATIC ${ALL_SOURCES})

target_link_libraries(core koopa pthread dl)

# compiler executable links the library and provides its own main
add_executable(compiler ${MAIN_SRC})
target_link_libraries(compiler core)

# Test target(s) - only build tests in Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_subdirectory(test)
else()
  message(STATUS "CMake build type is '${CMAKE_BUILD_TYPE}'; skipping building tests (tests only built for Debug).")
endif()