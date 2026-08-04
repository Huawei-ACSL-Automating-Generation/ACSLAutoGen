cmake_minimum_required(VERSION 3.16)

foreach(required_variable IN ITEMS ACSLG_EXECUTABLE SOURCE_DIR WORK_DIR)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "${required_variable} is required")
  endif()
endforeach()

function(require_contains file_path expected)
  file(READ "${file_path}" contents)
  string(FIND "${contents}" "${expected}" match_position)
  if(match_position EQUAL -1)
    message(FATAL_ERROR
            "${file_path} does not contain expected text:\n${expected}")
  endif()
endfunction()

function(require_not_contains file_path unexpected)
  file(READ "${file_path}" contents)
  string(FIND "${contents}" "${unexpected}" match_position)
  if(NOT match_position EQUAL -1)
    message(FATAL_ERROR
            "${file_path} unexpectedly contains text:\n${unexpected}")
  endif()
endfunction()

function(run_acslg source_path output_dir mode)
  file(MAKE_DIRECTORY "${output_dir}")
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}" -E env "ACSLG_NUMERICAL_INVARIANTS=${mode}"
      "${ACSLG_EXECUTABLE}" "${source_path}" --out-dir "${output_dir}"
      --log-level=warn --
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
            "ACSLG failed for ${source_path} in ${mode} mode (${result})\n"
            "stdout:\n${stdout}\nstderr:\n${stderr}")
  endif()
endfunction()

function(require_equal left_path right_path)
  file(SHA256 "${left_path}" left_hash)
  file(SHA256 "${right_path}" right_hash)
  if(NOT left_hash STREQUAL right_hash)
    message(FATAL_ERROR
            "nondeterministic ACSL output:\n${left_path}\n${right_path}")
  endif()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/input")

set(code2inv_2_source
    "${SOURCE_DIR}/benchmark/AutoSpec/code2inv_133_benchmark/2.c")
set(code2inv_120_source
    "${SOURCE_DIR}/benchmark/AutoSpec/code2inv_133_benchmark/120.c")
set(fib_30_source "${SOURCE_DIR}/benchmark/AutoSpec/fib_46_benchmark/30.c")

file(READ "${code2inv_2_source}" code2inv_2)
string(REPLACE "void main()" "int main()" code2inv_2 "${code2inv_2}")
file(WRITE "${WORK_DIR}/input/2.c" "#include <assert.h>\n${code2inv_2}")

file(READ "${code2inv_120_source}" code2inv_120)
file(WRITE "${WORK_DIR}/input/120.c" "#include <assert.h>\n${code2inv_120}")

file(READ "${fib_30_source}" fib_30)
string(REPLACE "static_assert(" "assert(" fib_30 "${fib_30}")
file(WRITE "${WORK_DIR}/input/30.c" "${fib_30}")

foreach(sample IN ITEMS 2 30 120)
  run_acslg("${WORK_DIR}/input/${sample}.c" "${WORK_DIR}/local-a" local)
  run_acslg("${WORK_DIR}/input/${sample}.c" "${WORK_DIR}/local-b" local)
endforeach()

set(code2inv_2_invariant
    "loop invariant acslg_polynomial: y >= 0 && y + 2 * x + -1 * y * y == 2;")
set(fib_30_invariant
    "loop invariant acslg_polynomial: i >= 0 && 2 * c + i + -1 * i * i == 0;")
set(code2inv_120_invariant
    "loop invariant acslg_polynomial: i >= 1 && sn - i == -1;")

require_contains("${WORK_DIR}/local-a/2_acsl.c" "${code2inv_2_invariant}")
require_contains("${WORK_DIR}/local-a/30_acsl.c" "${fib_30_invariant}")
require_contains("${WORK_DIR}/local-a/120_acsl.c" "${code2inv_120_invariant}")

foreach(sample IN ITEMS 2 30 120)
  require_equal("${WORK_DIR}/local-a/${sample}_acsl.c"
                "${WORK_DIR}/local-b/${sample}_acsl.c")
endforeach()

run_acslg("${WORK_DIR}/input/2.c" "${WORK_DIR}/disabled" disabled)
run_acslg("${WORK_DIR}/input/30.c" "${WORK_DIR}/disabled" disabled)
require_not_contains("${WORK_DIR}/disabled/2_acsl.c" "${code2inv_2_invariant}")
require_not_contains("${WORK_DIR}/disabled/30_acsl.c" "${fib_30_invariant}")

if(DEFINED FRAMA_C_EXECUTABLE AND NOT "${FRAMA_C_EXECUTABLE}" STREQUAL "" AND
   EXISTS "${FRAMA_C_EXECUTABLE}")
  foreach(sample IN ITEMS 2 30 120)
    execute_process(
      COMMAND "${FRAMA_C_EXECUTABLE}" "${WORK_DIR}/local-a/${sample}_acsl.c"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE stdout
      ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR
              "Frama-C rejected ${sample}_acsl.c (${result})\n"
              "stdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
  endforeach()

  execute_process(
    COMMAND
      "${FRAMA_C_EXECUTABLE}" "${WORK_DIR}/local-a/2_acsl.c" -wp
      -wp-prover z3 -wp-prop=acslg_polynomial -wp-timeout 10
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT result EQUAL 0 OR NOT stdout MATCHES "Proved goals:[ ]+2 / 2" OR
     NOT stdout MATCHES "Z3[^
]*1")
    message(FATAL_ERROR
            "Frama-C/WP did not prove the generated nonlinear invariant\n"
            "stdout:\n${stdout}\nstderr:\n${stderr}")
  endif()
else()
  message(STATUS "Frama-C not found; generated ACSL syntax check skipped")
endif()
