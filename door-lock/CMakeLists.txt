cmake_minimum_required(VERSION 3.20.0)

# tinytalk is the Zephyr module two directories up.
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tinytalk_door_lock)

target_sources(app PRIVATE src/main.c)

# The manual pairing code, for the boot log. scripts/spake2p_verifier.py
# re-derives the verifier from the passcode in this build's .config first, and
# the build stops if the two disagree: a code that does not match the verifier
# fails at Pake3 looking exactly like a typo.
execute_process(
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/spake2p_verifier.py
          --from-config ${DOTCONFIG}
  OUTPUT_VARIABLE TM_PAIRING_OUT
  ERROR_VARIABLE TM_PAIRING_ERR
  RESULT_VARIABLE TM_PAIRING_RC)
if(NOT TM_PAIRING_RC EQUAL 0)
  message(FATAL_ERROR "${TM_PAIRING_OUT}${TM_PAIRING_ERR}")
endif()
string(REGEX MATCH "setup code  ([0-9]+-[0-9]+-[0-9]+)" TM_PAIRING_MATCH "${TM_PAIRING_OUT}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "no setup code in: ${TM_PAIRING_OUT}")
endif()
message(STATUS "tinytalk manual pairing code: ${CMAKE_MATCH_1}")
target_compile_definitions(app PRIVATE TM_SAMPLE_MANUAL_CODE="${CMAKE_MATCH_1}")
