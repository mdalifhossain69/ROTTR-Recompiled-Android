cmake_minimum_required(VERSION 3.22)
project(rottr_recomp C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(rottr_runtime STATIC runtime/src/guest.c)
target_include_directories(rottr_runtime PUBLIC runtime/include)
target_compile_options(rottr_runtime PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_executable(test_guest tests/test_guest.c)
target_link_libraries(test_guest PRIVATE rottr_runtime)
add_test(NAME guest COMMAND test_guest)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME recon COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_recon.py)
  add_test(NAME functions COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_functions.py)
endif()
