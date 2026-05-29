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
file(GLOB_RECURSE CC_SOURCES CONFIGURE_DEPENDS "src/*.cc")
set(ALL_SOURCES ${C_SOURCES} ${_SOURCES} ${CC_SOURCES})

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
  enable_testing()

  add_executable(parser_basic_test test/parser_basic_test.cpp)
  target_link_libraries(parser_basic_test core)
  add_test(NAME parser_basic_test COMMAND parser_basic_test)

  add_executable(parser_unary_test test/parser_unary_test.cpp)
  target_link_libraries(parser_unary_test core)
  add_test(NAME parser_unary_test COMMAND parser_unary_test)

  add_executable(parser_binary_test test/parser_binary_test.cpp)
  target_link_libraries(parser_binary_test core)
  add_test(NAME parser_binary_test COMMAND parser_binary_test)

  add_executable(parser_decl_test test/parser_decl_test.cpp)
  target_link_libraries(parser_decl_test core)
  add_test(NAME parser_decl_test COMMAND parser_decl_test)

  add_executable(parser_assign_test test/parser_assign_test.cpp)
  target_link_libraries(parser_assign_test core)
  add_test(NAME parser_assign_test COMMAND parser_assign_test)

  add_executable(parser_stmt_test test/parser_stmt_test.cpp)
  target_link_libraries(parser_stmt_test core)
  add_test(NAME parser_stmt_test COMMAND parser_stmt_test)

  add_executable(parser_if_test test/parser_if_test.cpp)
  target_link_libraries(parser_if_test core)
  add_test(NAME parser_if_test COMMAND parser_if_test)

  add_executable(parser_while_test test/parser_while_test.cpp)
  target_link_libraries(parser_while_test core)
  add_test(NAME parser_while_test COMMAND parser_while_test)

  add_executable(parser_function_test test/parser_function_test.cpp)
  target_link_libraries(parser_function_test core)
  add_test(NAME parser_function_test COMMAND parser_function_test)

  add_executable(parser_array_test test/parser_array_test.cpp)
  target_link_libraries(parser_array_test core)
  add_test(NAME parser_array_test COMMAND parser_array_test)

  add_executable(parser_mint_test test/parser_mint_test.cpp)
  target_link_libraries(parser_mint_test core)
  add_test(NAME parser_mint_test COMMAND parser_mint_test)

  add_executable(semantic_symbol_test test/semantic_symbol_test.cpp)
  target_link_libraries(semantic_symbol_test core)
  add_test(NAME semantic_symbol_test COMMAND semantic_symbol_test)

  add_executable(semantic_const_fold_test test/semantic_const_fold_test.cpp)
  target_link_libraries(semantic_const_fold_test core)
  add_test(NAME semantic_const_fold_test COMMAND semantic_const_fold_test)

  add_executable(semantic_if_test test/semantic_if_test.cpp)
  target_link_libraries(semantic_if_test core)
  add_test(NAME semantic_if_test COMMAND semantic_if_test)

  add_executable(semantic_while_test test/semantic_while_test.cpp)
  target_link_libraries(semantic_while_test core)
  add_test(NAME semantic_while_test COMMAND semantic_while_test)

  add_executable(semantic_function_test test/semantic_function_test.cpp)
  target_link_libraries(semantic_function_test core)
  add_test(NAME semantic_function_test COMMAND semantic_function_test)

  add_executable(semantic_ssa_test test/semantic_ssa_test.cpp)
  target_link_libraries(semantic_ssa_test core)
  add_test(NAME semantic_ssa_test COMMAND semantic_ssa_test)

  add_executable(semantic_array_test test/semantic_array_test.cpp)
  target_link_libraries(semantic_array_test core)
  add_test(NAME semantic_array_test COMMAND semantic_array_test)

  add_executable(semantic_mint_test test/semantic_mint_test.cpp)
  target_link_libraries(semantic_mint_test core)
  add_test(NAME semantic_mint_test COMMAND semantic_mint_test)

  add_executable(koopa_basic_test test/koopa_basic_test.cpp)
  target_link_libraries(koopa_basic_test core)
  add_test(NAME koopa_basic_test COMMAND koopa_basic_test)

  add_executable(koopa_unary_test test/koopa_unary_test.cpp)
  target_link_libraries(koopa_unary_test core)
  add_test(NAME koopa_unary_test COMMAND koopa_unary_test)

  add_executable(koopa_binary_test test/koopa_binary_test.cpp)
  target_link_libraries(koopa_binary_test core)
  add_test(NAME koopa_binary_test COMMAND koopa_binary_test)

  add_executable(koopa_if_test test/koopa_if_test.cpp)
  target_link_libraries(koopa_if_test core)
  add_test(NAME koopa_if_test COMMAND koopa_if_test)

  add_executable(koopa_while_test test/koopa_while_test.cpp)
  target_link_libraries(koopa_while_test core)
  add_test(NAME koopa_while_test COMMAND koopa_while_test)

  add_executable(koopa_decl_test test/koopa_decl_test.cpp)
  target_link_libraries(koopa_decl_test core)
  add_test(NAME koopa_decl_test COMMAND koopa_decl_test)

  add_executable(koopa_function_test test/koopa_function_test.cpp)
  target_link_libraries(koopa_function_test core)
  add_test(NAME koopa_function_test COMMAND koopa_function_test)

  add_executable(koopa_ssa_test test/koopa_ssa_test.cpp)
  target_link_libraries(koopa_ssa_test core)
  add_test(NAME koopa_ssa_test COMMAND koopa_ssa_test)

  add_executable(koopa_array_test test/koopa_array_test.cpp)
  target_link_libraries(koopa_array_test core)
  add_test(NAME koopa_array_test COMMAND koopa_array_test)

  add_executable(koopa_mint_test test/koopa_mint_test.cpp)
  target_link_libraries(koopa_mint_test core)
  add_test(NAME koopa_mint_test COMMAND koopa_mint_test)

  add_executable(backend_riscv_test test/backend_riscv_test.cpp)
  target_link_libraries(backend_riscv_test core)
  add_test(NAME backend_riscv_test COMMAND backend_riscv_test)

  add_executable(compiler_llvm_test test/compiler_llvm_test.cpp)
  target_link_libraries(compiler_llvm_test core)
  add_test(NAME compiler_llvm_test COMMAND compiler_llvm_test)
else()
  message(STATUS "CMake build type is '${CMAKE_BUILD_TYPE}'; skipping building tests (tests only built for Debug).")
endif()