cmake_minimum_required(VERSION 3.22)
project(rottr_recomp C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(rottr_runtime STATIC
    runtime/src/guest.c
    runtime/src/win32_stubs.cpp
    runtime/src/remill_intrinsics.cpp
    runtime/src/graphics/graphics.cpp
    runtime/src/audio/audio.cpp
    runtime/src/input/input.cpp
    runtime/src/video/video.cpp
)
target_include_directories(rottr_runtime PUBLIC runtime/include runtime/src)
target_compile_options(rottr_runtime PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_executable(test_guest tests/test_guest.c)
target_link_libraries(test_guest PRIVATE rottr_runtime)
add_test(NAME guest COMMAND test_guest)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME recon COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_recon.py)
  add_test(NAME functions COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_functions.py)
  add_test(NAME harness COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_harness.py)
endif()
