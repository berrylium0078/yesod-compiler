# Tests for the yesod compiler project
enable_testing()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(parser_basic_test parser/parser_basic_test.cpp)
target_link_libraries(parser_basic_test core)
add_test(NAME parser_basic_test COMMAND parser_basic_test)

add_executable(parser_unary_test parser/parser_unary_test.cpp)
target_link_libraries(parser_unary_test core)
add_test(NAME parser_unary_test COMMAND parser_unary_test)

add_executable(parser_binary_test parser/parser_binary_test.cpp)
target_link_libraries(parser_binary_test core)
add_test(NAME parser_binary_test COMMAND parser_binary_test)

add_executable(parser_decl_test parser/parser_decl_test.cpp)
target_link_libraries(parser_decl_test core)
add_test(NAME parser_decl_test COMMAND parser_decl_test)

add_executable(parser_assign_test parser/parser_assign_test.cpp)
target_link_libraries(parser_assign_test core)
add_test(NAME parser_assign_test COMMAND parser_assign_test)

add_executable(parser_stmt_test parser/parser_stmt_test.cpp)
target_link_libraries(parser_stmt_test core)
add_test(NAME parser_stmt_test COMMAND parser_stmt_test)

add_executable(parser_if_test parser/parser_if_test.cpp)
target_link_libraries(parser_if_test core)
add_test(NAME parser_if_test COMMAND parser_if_test)

add_executable(parser_while_test parser/parser_while_test.cpp)
target_link_libraries(parser_while_test core)
add_test(NAME parser_while_test COMMAND parser_while_test)

add_executable(parser_function_test parser/parser_function_test.cpp)
target_link_libraries(parser_function_test core)
add_test(NAME parser_function_test COMMAND parser_function_test)

add_executable(parser_array_test parser/parser_array_test.cpp)
target_link_libraries(parser_array_test core)
add_test(NAME parser_array_test COMMAND parser_array_test)

add_executable(parser_mint_test parser/parser_mint_test.cpp)
target_link_libraries(parser_mint_test core)
add_test(NAME parser_mint_test COMMAND parser_mint_test)

add_executable(parser_poly_test parser/parser_poly_test.cpp)
target_link_libraries(parser_poly_test core)
add_test(NAME parser_poly_test COMMAND parser_poly_test)

add_executable(semantic_symbol_test semantic/semantic_symbol_test.cpp)
target_link_libraries(semantic_symbol_test core)
add_test(NAME semantic_symbol_test COMMAND semantic_symbol_test)

add_executable(semantic_const_fold_test semantic/semantic_const_fold_test.cpp)
target_link_libraries(semantic_const_fold_test core)
add_test(NAME semantic_const_fold_test COMMAND semantic_const_fold_test)

add_executable(semantic_if_test semantic/semantic_if_test.cpp)
target_link_libraries(semantic_if_test core)
add_test(NAME semantic_if_test COMMAND semantic_if_test)

add_executable(semantic_while_test semantic/semantic_while_test.cpp)
target_link_libraries(semantic_while_test core)
add_test(NAME semantic_while_test COMMAND semantic_while_test)

add_executable(semantic_function_test semantic/semantic_function_test.cpp)
target_link_libraries(semantic_function_test core)
add_test(NAME semantic_function_test COMMAND semantic_function_test)

add_executable(semantic_ssa_test semantic/semantic_ssa_test.cpp)
target_link_libraries(semantic_ssa_test core)
add_test(NAME semantic_ssa_test COMMAND semantic_ssa_test)

add_executable(semantic_array_test semantic/semantic_array_test.cpp)
target_link_libraries(semantic_array_test core)
add_test(NAME semantic_array_test COMMAND semantic_array_test)

add_executable(semantic_mint_test semantic/semantic_mint_test.cpp)
target_link_libraries(semantic_mint_test core)
add_test(NAME semantic_mint_test COMMAND semantic_mint_test)

add_executable(koopa_basic_test koopa/koopa_basic_test.cpp)
target_link_libraries(koopa_basic_test core)
add_test(NAME koopa_basic_test COMMAND koopa_basic_test)

add_executable(koopa_unary_test koopa/koopa_unary_test.cpp)
target_link_libraries(koopa_unary_test core)
add_test(NAME koopa_unary_test COMMAND koopa_unary_test)

add_executable(koopa_binary_test koopa/koopa_binary_test.cpp)
target_link_libraries(koopa_binary_test core)
add_test(NAME koopa_binary_test COMMAND koopa_binary_test)

add_executable(koopa_if_test koopa/koopa_if_test.cpp)
target_link_libraries(koopa_if_test core)
add_test(NAME koopa_if_test COMMAND koopa_if_test)

add_executable(koopa_while_test koopa/koopa_while_test.cpp)
target_link_libraries(koopa_while_test core)
add_test(NAME koopa_while_test COMMAND koopa_while_test)

add_executable(koopa_decl_test koopa/koopa_decl_test.cpp)
target_link_libraries(koopa_decl_test core)
add_test(NAME koopa_decl_test COMMAND koopa_decl_test)

add_executable(koopa_function_test koopa/koopa_function_test.cpp)
target_link_libraries(koopa_function_test core)
add_test(NAME koopa_function_test COMMAND koopa_function_test)

add_executable(koopa_ssa_test koopa/koopa_ssa_test.cpp)
target_link_libraries(koopa_ssa_test core)
add_test(NAME koopa_ssa_test COMMAND koopa_ssa_test)

add_executable(koopa_array_test koopa/koopa_array_test.cpp)
target_link_libraries(koopa_array_test core)
add_test(NAME koopa_array_test COMMAND koopa_array_test)

add_executable(koopa_mint_test koopa/koopa_mint_test.cpp)
target_link_libraries(koopa_mint_test core)
add_test(NAME koopa_mint_test COMMAND koopa_mint_test)

add_executable(koopa_poly_test koopa/koopa_poly_test.cpp)
target_link_libraries(koopa_poly_test core)
add_test(NAME koopa_poly_test COMMAND koopa_poly_test)

add_executable(koopa_extra_test
  koopa/koopa_extra_test.cpp
  koopa/koopa_interpreter.cpp
  interpreter_runner.cpp
)
target_link_libraries(koopa_extra_test core)
target_compile_definitions(koopa_extra_test
  PRIVATE YESOD_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

set(POLY_INTERPRETER_SOURCES
  interpreter_runner.cpp
  poly/poly_runtime.cpp
  poly/poly_interpreter.cpp
  poly/poly_test_driver.cpp
)

add_executable(poly_base_extra_test
  poly/poly_base_extra_test.cpp
  ${POLY_INTERPRETER_SOURCES}
)
target_link_libraries(poly_base_extra_test core)
target_compile_definitions(poly_base_extra_test
  PRIVATE YESOD_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(poly_extra_test
  poly/poly_extra_test.cpp
  ${POLY_INTERPRETER_SOURCES}
)
target_link_libraries(poly_extra_test core)
target_compile_definitions(poly_extra_test
  PRIVATE YESOD_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(compiler_llvm_test compiler_llvm_test.cpp)
target_link_libraries(compiler_llvm_test core)
add_test(NAME compiler_llvm_test COMMAND compiler_llvm_test)

# ------------------------------------------------------------------
# Build SysY files in test/llvm/ to LLVM IR and produce RISC-V ELFs
# ------------------------------------------------------------------
set(LLVM_SY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/llvm)
file(GLOB LLVM_SY_FILES CONFIGURE_DEPENDS "${LLVM_SY_DIR}/*.sy")

if(NOT LLVM_SY_FILES)
  message(STATUS "No .sy files found under ${LLVM_SY_DIR}; skipping llvm build targets.")
else()
  set(LLVM_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/llvm)
  file(MAKE_DIRECTORY ${LLVM_OUT_DIR})

  if(NOT DEFINED ENV{CDE_LIBRARY_PATH})
    message(WARNING "CDE_LIBRARY_PATH is not set; RISC-V linking may fail for llvm build targets.")
  endif()

  set(LLVM_ELF_TARGETS)
  foreach(SY_FILE IN LISTS LLVM_SY_FILES)
    get_filename_component(SY_NAME ${SY_FILE} NAME_WE)
    set(OUT_LL ${LLVM_OUT_DIR}/${SY_NAME}.ll)
    set(OUT_S ${LLVM_OUT_DIR}/${SY_NAME}.S)
    set(OUT_O ${LLVM_OUT_DIR}/${SY_NAME}.o)
    set(OUT_ELF ${LLVM_OUT_DIR}/${SY_NAME}.elf)

    add_custom_command(
      OUTPUT ${OUT_ELF}
      COMMAND $<TARGET_FILE:compiler> -llvm ${SY_FILE} -o ${OUT_LL}
      COMMAND clang ${OUT_LL} -S -o ${OUT_S} -target riscv32-unknown-linux-elf -march=rv32im -mabi=ilp32
      COMMAND clang ${OUT_S} -c -o ${OUT_O} -target riscv32-unknown-linux-elf -march=rv32im -mabi=ilp32
      COMMAND ld.lld ${OUT_O} -L$ENV{CDE_LIBRARY_PATH}/riscv32 -lsysy -o ${OUT_ELF}
      DEPENDS compiler
      WORKING_DIRECTORY ${LLVM_OUT_DIR}
      COMMENT "Compiling ${SY_FILE} -> ${OUT_ELF} (via LLVM IR and RISC-V toolchain)"
    )

    list(APPEND LLVM_ELF_TARGETS ${OUT_ELF})
  endforeach()

  add_custom_target(llvm_build_all DEPENDS ${LLVM_ELF_TARGETS})
  add_custom_target(llvm_build_verbose ALL DEPENDS llvm_build_all)
endif()
