if(NOT DEFINED MINI_RUNNER OR NOT DEFINED OFFICIAL_RUNNER OR NOT DEFINED SCRIPT)
  message(FATAL_ERROR "MINI_RUNNER, OFFICIAL_RUNNER, and SCRIPT are required")
endif()

execute_process(
  COMMAND "${MINI_RUNNER}" "${SCRIPT}"
  RESULT_VARIABLE mini_result
  OUTPUT_VARIABLE mini_output
  ERROR_VARIABLE mini_error)
if(NOT mini_result EQUAL 0)
  message(FATAL_ERROR "mini_as failed (${mini_result}): ${mini_error}")
endif()

execute_process(
  COMMAND "${OFFICIAL_RUNNER}" "${SCRIPT}"
  RESULT_VARIABLE official_result
  OUTPUT_VARIABLE official_output
  ERROR_VARIABLE official_error)
if(NOT official_result EQUAL 0)
  message(FATAL_ERROR "AngelScript failed (${official_result}): ${official_error}")
endif()

string(REPLACE "\r\n" "\n" mini_output "${mini_output}")
string(REPLACE "\r\n" "\n" official_output "${official_output}")
if(NOT mini_output STREQUAL official_output)
  message(FATAL_ERROR
    "Differential mismatch for ${SCRIPT}\nmini_as:\n${mini_output}\nAngelScript:\n${official_output}")
endif()
