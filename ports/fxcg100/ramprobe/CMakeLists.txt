cmake_minimum_required(VERSION 3.15)
project(cg50_ramprobe C)

include(GenerateG3A)
find_package(Gint 2.9 REQUIRED)

set(CGBA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
execute_process(
  COMMAND git describe --always --dirty
  WORKING_DIRECTORY "${CGBA_ROOT}"
  OUTPUT_VARIABLE RAMPROBE_BUILD_ID
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT RAMPROBE_BUILD_ID)
  set(RAMPROBE_BUILD_ID "unknown")
endif()

add_executable(cg50-ramprobe src/main.c)
target_compile_definitions(cg50-ramprobe PRIVATE
  RAMPROBE_BUILD_ID="${RAMPROBE_BUILD_ID}")
target_compile_options(cg50-ramprobe PRIVATE
  -O2 -fomit-frame-pointer -ffunction-sections -fdata-sections
  -Wall -Wextra -Wshadow -Wconversion)
target_link_options(cg50-ramprobe PRIVATE -Wl,--gc-sections)
target_link_libraries(cg50-ramprobe Gint::Gint)

generate_g3a(TARGET cg50-ramprobe OUTPUT "RAMProbe-CG50.g3a"
  NAME "RAM Probe" VERSION "00.01.0000")
