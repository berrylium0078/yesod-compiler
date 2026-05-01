cmake_minimum_required(VERSION 3.13)
project(compiler)

# settings
# set to OFF to enable C mode
set(CPP_MODE ON)
if(CPP_MODE)
  set(FB_EXT ".cpp")
else()
  set(FB_EXT ".c")
endif()
message(STATUS "Flex/Bison generated source file extension: ${FB_EXT}")

# enable all warnings
if(MSVC)
  add_compile_options(/W3)
else()
  # disable warnings caused by old version of Flex
  add_compile_options(-Wall -Wno-register)
endif()

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
file(GLOB_RECURSE CXX_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE CC_SOURCES CONFIGURE_DEPENDS "src/*.cc")
set(ALL_SOURCES ${C_SOURCES} ${CXX_SOURCES} ${CC_SOURCES})

# Split out main so we can build a reusable static library for tests.
set(MAIN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
list(REMOVE_ITEM ALL_SOURCES ${MAIN_SRC})

# core static library used by the compiler and tests
add_library(core STATIC ${ALL_SOURCES})
set_target_properties(core PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(core koopa pthread dl)

# compiler executable links the library and provides its own main
add_executable(compiler ${MAIN_SRC})
set_target_properties(compiler PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(compiler core)

# Test target(s)
enable_testing()

add_executable(parser_test test/parser_test.cpp)
set_target_properties(parser_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(parser_test core)

add_test(NAME parser_test COMMAND parser_test)

add_executable(koopa_generator_test test/koopa_generator_test.cpp)
set_target_properties(koopa_generator_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(koopa_generator_test core)

add_test(NAME koopa_generator_test COMMAND koopa_generator_test)