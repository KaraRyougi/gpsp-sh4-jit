cmake_minimum_required(VERSION 3.15)
project(classpad_hzprobe C)

# ClassPad II LCD refresh rate & VRR hardware probe (HollyHock-3 .hh3).
# Configure with the fxSDK FXCP toolchain (see build.sh).
find_package(Gint 2.9 REQUIRED)

add_executable(classpad-hzprobe src/main.c)
target_include_directories(classpad-hzprobe PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_options(classpad-hzprobe PRIVATE
  -O2 -fomit-frame-pointer -ffunction-sections -fdata-sections
  -Wall -Wextra -Wshadow)
target_link_options(classpad-hzprobe PRIVATE -Wl,--gc-sections)
target_link_libraries(classpad-hzprobe Gint::Gint)

# A HollyHock-3 add-in is the stripped ELF.
string(REPLACE "gcc" "strip" HZPROBE_STRIP "${CMAKE_C_COMPILER}")
add_custom_command(TARGET classpad-hzprobe POST_BUILD
  COMMAND ${HZPROBE_STRIP} -s -o "${CMAKE_CURRENT_SOURCE_DIR}/HZProbe.hh3"
    "$<TARGET_FILE:classpad-hzprobe>")
