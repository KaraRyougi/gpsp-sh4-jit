cmake_minimum_required(VERSION 3.15)
project(cgba_gint_smoke C)

include(GenerateG3A)
find_package(Gint 2.9 REQUIRED)

add_executable(cgba-gint-smoke src/main.c)
target_compile_options(cgba-gint-smoke PRIVATE -Wall -Wextra -Os -g)
target_link_libraries(cgba-gint-smoke Gint::Gint)

generate_g3a(TARGET cgba-gint-smoke OUTPUT "gpSP-GINT.g3a"
  NAME "gpSP-GINT" VERSION "00.01.0000")
