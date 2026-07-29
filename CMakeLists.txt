cmake_minimum_required(VERSION 3.16)
project(mini_angelscript VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mini_angelscript
  src/core.cpp
  src/engine.cpp
  src/interpreter.cpp
  src/parser.cpp
  src/tokenizer.cpp
  src/type_checker.cpp
)
target_include_directories(mini_angelscript PUBLIC include)

if(MSVC)
  target_compile_options(mini_angelscript PRIVATE /W4 /permissive-)
else()
  target_compile_options(mini_angelscript PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(mini_as_tests
  tests/test_main.cpp
  tests/test_core.cpp
  tests/test_tokenizer.cpp
  tests/test_parser.cpp
  tests/test_interpreter.cpp
  tests/test_types.cpp
)
target_link_libraries(mini_as_tests PRIVATE mini_angelscript)
add_test(NAME mini_as_tests COMMAND mini_as_tests)

add_executable(m0_demo examples/m0/main.cpp)
target_link_libraries(m0_demo PRIVATE mini_angelscript)
