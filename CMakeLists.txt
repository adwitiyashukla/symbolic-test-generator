cmake_minimum_required(VERSION 3.16)
project(symbolic_test_generator CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(SYMX_WERROR "Treat warnings as errors" OFF)

if(MSVC)
  add_compile_options(/W3 /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
  if(SYMX_WERROR)
    add_compile_options(-Werror)
  endif()
endif()

add_library(symx_core STATIC
  src/emit/emit.cpp
  src/exec/executor.cpp
  src/exec/memory.cpp
  src/exec/search.cpp
  src/ir/ir.cpp
  src/ir/lexer.cpp
  src/ir/lower.cpp
  src/ir/parser.cpp
  src/solver/bitblast.cpp
  src/solver/expr.cpp
  src/solver/sat.cpp
  src/solver/solver.cpp
  src/util/config.cpp
  src/util/support.cpp)

target_include_directories(symx_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(symx src/cli/main.cpp src/cli/verify.cpp)
target_link_libraries(symx PRIVATE symx_core)

enable_testing()

set(SYMX_TESTS
  config_test
  executor_test
  expr_test
  frontend_test
  memory_test
  sat_test
  solver_test)

foreach(name IN LISTS SYMX_TESTS)
  add_executable(${name} tests/${name}.cpp)
  target_link_libraries(${name} PRIVATE symx_core)
  add_test(NAME ${name} COMMAND ${name})
endforeach()
