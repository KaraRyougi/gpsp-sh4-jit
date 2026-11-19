cmake_minimum_required(VERSION 3.15)
project(classpad_lcdbench C)

# fx-CP400 LCD transfer benchmark (HollyHock-3 .hh3). Configure with the
# fxSDK FXCP toolchain:  fxsdk build-cp
find_package(Gint 2.9 REQUIRED)

add_executable(classpad-lcdbench src/main.c)
target_include_directories(classpad-lcdbench PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_options(classpad-lcdbench PRIVATE
  -O2 -fomit-frame-pointer -ffunction-sections -fdata-sections
  -Wall -Wextra -Wshadow)
target_link_options(classpad-lcdbench PRIVATE -Wl,--gc-sections)
target_link_libraries(classpad-lcdbench Gint::Gint)

# A HollyHock-3 add-in is the stripped ELF.
string(REPLACE "gcc" "strip" LCDBENCH_STRIP "${CMAKE_C_COMPILER}")
add_custom_command(TARGET classpad-lcdbench POST_BUILD
  COMMAND ${LCDBENCH_STRIP} -s -o "${CMAKE_CURRENT_SOURCE_DIR}/LCDBench.hh3"
    "$<TARGET_FILE:classpad-lcdbench>")
