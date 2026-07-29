cmake_minimum_required(VERSION 3.16)
project(mini_angelscript VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mini_angelscript
  src/core.cpp
  src/engine.cpp
  src/tokenizer.cpp
)
target_include_directories(mini_angelscript PUBLIC include)

if(MSVC)
  target_compile_options(mini_angelscript PRIVATE /W4 /permissive-)
else()
  target_compile_options(mini_angelscript PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(mini_as_tests tests/test_main.cpp)
target_link_libraries(mini_as_tests PRIVATE mini_angelscript)
add_test(NAME mini_as_tests COMMAND mini_as_tests)
