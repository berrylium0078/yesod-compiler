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

add_executable(koopa_extra_test
  koopa/koopa_extra_test.cpp
  koopa/koopa_interpreter.cpp
  poly/poly_runtime.cpp
  interpreter_runner.cpp
)
target_link_libraries(koopa_extra_test core)
target_compile_definitions(koopa_extra_test
  PRIVATE YESOD_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(koopa_ir_validation_test
  koopa/koopa_ir_validation_test.cpp
  koopa/koopa_interpreter.cpp
  poly/poly_runtime.cpp
)
target_link_libraries(koopa_ir_validation_test core)
add_test(NAME koopa_ir_validation_test COMMAND koopa_ir_validation_test)

foreach(KOOPA_LEVEL IN ITEMS lv1 lv3 lv4 lv5 lv6 lv7 lv8 lv9)
  add_test(
    NAME koopa_${KOOPA_LEVEL}_test
    COMMAND koopa_extra_test
      "${CMAKE_CURRENT_SOURCE_DIR}/testsuit-collection/${KOOPA_LEVEL}"
      --timeout-ms 5000
  )
endforeach()

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
add_dependencies(compiler_llvm_test compiler)
target_compile_definitions(compiler_llvm_test
  PRIVATE YESOD_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

foreach(LLVM_LEVEL IN ITEMS lv1 lv3 lv4 lv5 lv6 lv7 lv8 lv9)
  add_test(
    NAME compiler_llvm_${LLVM_LEVEL}_test
    COMMAND compiler_llvm_test
      "${CMAKE_CURRENT_SOURCE_DIR}/testsuit-collection/${LLVM_LEVEL}"
      --timeout-seconds 10
  )
endforeach()

add_test(
  NAME compiler_llvm_poly_test
  COMMAND compiler_llvm_test
    "${CMAKE_CURRENT_SOURCE_DIR}/testsuit-collection/poly"
    --timeout-seconds 60
)
