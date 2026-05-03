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

add_executable(parser_basic_test test/parser_basic_test.cpp)
set_target_properties(parser_basic_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(parser_basic_test core)
add_test(NAME parser_basic_test COMMAND parser_basic_test)

add_executable(parser_unary_test test/parser_unary_test.cpp)
set_target_properties(parser_unary_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(parser_unary_test core)
add_test(NAME parser_unary_test COMMAND parser_unary_test)

add_executable(parser_binary_test test/parser_binary_test.cpp)
set_target_properties(parser_binary_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(parser_binary_test core)
add_test(NAME parser_binary_test COMMAND parser_binary_test)

add_executable(parser_decl_test test/parser_decl_test.cpp)
set_target_properties(parser_decl_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(parser_decl_test core)
add_test(NAME parser_decl_test COMMAND parser_decl_test)

add_executable(parser_assign_test test/parser_assign_test.cpp)
set_target_properties(parser_assign_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(parser_assign_test core)
add_test(NAME parser_assign_test COMMAND parser_assign_test)

add_executable(parser_stmt_test test/parser_stmt_test.cpp)
set_target_properties(parser_stmt_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(parser_stmt_test core)
add_test(NAME parser_stmt_test COMMAND parser_stmt_test)

add_executable(semantic_symbol_test test/semantic_symbol_test.cpp)
set_target_properties(semantic_symbol_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(semantic_symbol_test core)
add_test(NAME semantic_symbol_test COMMAND semantic_symbol_test)

add_executable(semantic_const_fold_test test/semantic_const_fold_test.cpp)
set_target_properties(semantic_const_fold_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(semantic_const_fold_test core)
add_test(NAME semantic_const_fold_test COMMAND semantic_const_fold_test)

add_executable(koopa_basic_test test/koopa_basic_test.cpp)
set_target_properties(koopa_basic_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(koopa_basic_test core)
add_test(NAME koopa_basic_test COMMAND koopa_basic_test)

add_executable(koopa_unary_test test/koopa_unary_test.cpp)
set_target_properties(koopa_unary_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(koopa_unary_test core)
add_test(NAME koopa_unary_test COMMAND koopa_unary_test)

add_executable(koopa_binary_test test/koopa_binary_test.cpp)
set_target_properties(koopa_binary_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(koopa_binary_test core)
add_test(NAME koopa_binary_test COMMAND koopa_binary_test)

add_executable(koopa_decl_test test/koopa_decl_test.cpp)
set_target_properties(koopa_decl_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(koopa_decl_test core)
add_test(NAME koopa_decl_test COMMAND koopa_decl_test)

add_executable(backend_riscv_test test/backend_riscv_test.cpp)
set_target_properties(backend_riscv_test PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
target_link_libraries(backend_riscv_test core)
add_test(NAME backend_riscv_test COMMAND backend_riscv_test)