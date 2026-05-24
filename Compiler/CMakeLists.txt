cmake_minimum_required(VERSION 3.16)

project(sysy_ast_ir_demo LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

add_library(sysy_ast STATIC
  src/ast.cpp
)

target_include_directories(sysy_ast
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(ast_ir_demo
  examples/ast_ir_demo.cpp
)

target_link_libraries(ast_ir_demo
  PRIVATE
    sysy_ast
)

BISON_TARGET(SysYParser
  src/sysy.y
  ${CMAKE_CURRENT_BINARY_DIR}/sysy_parser.cpp
  DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/sysy_parser.hpp
)

FLEX_TARGET(SysYLexer
  src/sysy.l
  ${CMAKE_CURRENT_BINARY_DIR}/sysy_lexer.cpp
)

ADD_FLEX_BISON_DEPENDENCY(SysYLexer SysYParser)

add_executable(compiler
  src/main.cpp
  src/backend/riscv.cpp
  ${BISON_SysYParser_OUTPUTS}
  ${FLEX_SysYLexer_OUTPUTS}
)

target_include_directories(compiler
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    /opt/include
)

target_link_libraries(compiler
  PRIVATE
    sysy_ast
    koopa
)

target_link_directories(compiler
  PRIVATE
    /opt/lib/native
)

if(MSVC)
  target_compile_options(sysy_ast PRIVATE /utf-8)
  target_compile_options(ast_ir_demo PRIVATE /utf-8)
  target_compile_options(compiler PRIVATE /utf-8)
endif()
