cmake_minimum_required(VERSION 3.16)
project(mini_angelscript VERSION 0.1.0 LANGUAGES CXX)

option(MINI_AS_BUILD_COMPAT_TESTS
  "Build differential tests against AngelScript 2.38.0" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mini_angelscript
  src/bytecode.cpp
  src/constant_evaluator.cpp
  src/core.cpp
  src/engine.cpp
  src/generic.cpp
  src/interpreter.cpp
  src/object.cpp
  src/parser.cpp
  src/tokenizer.cpp
  src/type_checker.cpp
  src/vm.cpp
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
  tests/test_bytecode.cpp
  tests/test_constant_evaluator.cpp
  tests/test_vm.cpp
  tests/test_engine.cpp
  tests/test_generic.cpp
  tests/test_objects.cpp
  tests/test_gc.cpp
  tests/test_tutorial.cpp
)
target_link_libraries(mini_as_tests PRIVATE mini_angelscript)
add_test(NAME mini_as_tests COMMAND mini_as_tests)

add_executable(m0_demo examples/m0/main.cpp)
target_link_libraries(m0_demo PRIVATE mini_angelscript)

add_executable(tutorial_clone examples/tutorial/main.cpp)
target_link_libraries(tutorial_clone PRIVATE mini_angelscript)
add_test(NAME tutorial_clone
  COMMAND tutorial_clone ${CMAKE_CURRENT_SOURCE_DIR}/examples/tutorial/script.as)

if(MINI_AS_BUILD_COMPAT_TESTS)
  include(FetchContent)
  FetchContent_Declare(angelscript_238
    URL https://www.angelcode.com/angelscript/sdk/files/angelscript_2.38.0.zip
    URL_HASH SHA256=b33b5dbcda10317ef67d628353d83246984ce6fcac102d4dc2aed121eba52e6f
    SOURCE_SUBDIR angelscript/projects/cmake
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(AS_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(angelscript_238)

  add_executable(mini_as_compat_runner tests/compat/mini_runner.cpp)
  target_link_libraries(mini_as_compat_runner PRIVATE mini_angelscript)

  add_executable(angelscript_compat_runner tests/compat/official_runner.cpp)
  target_link_libraries(angelscript_compat_runner PRIVATE angelscript)

  add_test(NAME compat_arithmetic
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/arithmetic.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_multiple_declarations
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/multiple_declarations.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_const_variables
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/const_variables.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_auto_declarations
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/auto_declarations.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_module_globals
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/module_globals.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_for_loops
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/for_loops.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_do_while_loops
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/do_while_loops.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_switch_fallthrough
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/switch_fallthrough.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_break_statements
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/break_statements.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_continue_statements
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/continue_statements.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_compound_assignments
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/compound_assignments.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_increment_expressions
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/increment_expressions.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_conditional_expressions
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/conditional_expressions.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_instance_methods
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/instance_methods.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_overloaded_constructors
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/overloaded_constructors.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_field_initializers
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/field_initializers.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_virtual_interface_dispatch
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/virtual_interface_dispatch.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_v02_acceptance
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/v02_acceptance.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_integer_family
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/integer_family.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_double_precision
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/double_precision.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_numeric_literals
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/numeric_literals.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_bitwise_shifts
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/bitwise_shifts.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_exponent_operators
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/exponent_operators.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_enums
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/enums.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_typedefs
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/typedefs.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_namespaces
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/namespaces.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_default_arguments
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/default_arguments.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_named_arguments
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/named_arguments.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_reference_parameters
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/reference_parameters.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_return_references
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/return_references.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_script_destructors
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/script_destructors.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)

  add_test(NAME compat_single_inheritance
    COMMAND ${CMAKE_COMMAND}
      -DMINI_RUNNER=$<TARGET_FILE:mini_as_compat_runner>
      -DOFFICIAL_RUNNER=$<TARGET_FILE:angelscript_compat_runner>
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/cases/single_inheritance.as
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat/compare.cmake)
endif()
