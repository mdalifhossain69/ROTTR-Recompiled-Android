cmake_minimum_required(VERSION 3.22)
project(rottr_recomp C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core runtime library
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

# Android-specific: native activity entry point
if(ANDROID)
    add_library(rottr_android SHARED
        runtime/src/android_main.cpp
    )
    target_link_libraries(rottr_android PRIVATE rottr_runtime android log OpenSLES)
    target_include_directories(rottr_android PRIVATE runtime/include runtime/src)
    target_compile_options(rottr_android PRIVATE -Wall -Wextra -Werror)
    set_target_properties(rottr_android PROPERTIES
        PREFIX ""
        SUFFIX ".so"
        ANDROID_SO_UNDEFINED OFF
    )
endif()

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
