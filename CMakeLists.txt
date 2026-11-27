cmake_minimum_required(VERSION 3.22)
project(rottr_recomp C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Portable part: guest state, Remill helpers, Win32 stubs and runtime probes.
add_library(rottr_core STATIC
    ${CMAKE_CURRENT_LIST_DIR}/runtime/src/guest.c
    ${CMAKE_CURRENT_LIST_DIR}/runtime/src/remill_intrinsics.cpp
    ${CMAKE_CURRENT_LIST_DIR}/runtime/src/win32_stubs.cpp
    ${CMAKE_CURRENT_LIST_DIR}/runtime/src/x64_runtime.cpp
)
target_include_directories(rottr_core PUBLIC ${CMAKE_CURRENT_LIST_DIR}/runtime/include)
target_compile_options(rottr_core PRIVATE -Wall -Wextra -Werror)
set_target_properties(rottr_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(ANDROID)
    add_library(rottr_runtime STATIC
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/graphics/graphics.cpp
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/audio/audio.cpp
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/input/input.cpp
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/input/touch_controls.cpp
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/video/video.cpp
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/performance.cpp
    )
    target_include_directories(rottr_runtime PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/runtime/include ${CMAKE_CURRENT_LIST_DIR}/runtime/src)
    target_link_libraries(rottr_runtime PUBLIC rottr_core)
    target_compile_options(rottr_runtime PRIVATE -Wall -Wextra -Werror)
    set_target_properties(rottr_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

    add_library(rottr_android SHARED ${CMAKE_CURRENT_LIST_DIR}/runtime/src/android_main.cpp
                                       ${CMAKE_CURRENT_LIST_DIR}/runtime/src/jni_bridge.cpp
                                       ${CMAKE_CURRENT_LIST_DIR}/runtime/src/vk/vulkan_renderer.cpp
                                       ${CMAKE_CURRENT_LIST_DIR}/runtime/src/pe_inspect.cpp)
    target_include_directories(rottr_android PRIVATE ${CMAKE_CURRENT_LIST_DIR}/runtime/include)
    target_link_libraries(rottr_android PRIVATE rottr_runtime android log OpenSLES vulkan aaudio)
    target_compile_options(rottr_android PRIVATE -Wall -Wextra -Werror)
    set_target_properties(rottr_android PROPERTIES ANDROID_SO_UNDEFINED OFF)
    return()
endif()

enable_testing()
add_executable(test_guest tests/test_guest.c)
target_link_libraries(test_guest PRIVATE rottr_core)
add_test(NAME guest COMMAND test_guest)

add_executable(test_pe_inspect tests/test_pe_inspect.cpp runtime/src/pe_inspect.cpp)
target_include_directories(test_pe_inspect PRIVATE runtime/include)
target_compile_features(test_pe_inspect PRIVATE cxx_std_17)
target_compile_options(test_pe_inspect PRIVATE -Wall -Wextra -Werror)
add_test(NAME pe_inspect COMMAND test_pe_inspect)

add_executable(test_runtime tests/test_runtime.cpp)
target_link_libraries(test_runtime PRIVATE rottr_core)
target_compile_options(test_runtime PRIVATE -Wall -Wextra -Werror)
add_test(NAME runtime COMMAND test_runtime)

add_executable(test_x64_runtime tests/test_x64_runtime.cpp)
target_link_libraries(test_x64_runtime PRIVATE rottr_core)
target_compile_options(test_x64_runtime PRIVATE -Wall -Wextra -Werror)
add_test(NAME x64_runtime COMMAND test_x64_runtime)

find_package(Vulkan)
if(Vulkan_FOUND)
  add_executable(test_vulkan tests/test_vulkan.cpp runtime/src/vk/vulkan_renderer.cpp)
  target_include_directories(test_vulkan PRIVATE runtime/include)
  target_link_libraries(test_vulkan PRIVATE Vulkan::Vulkan)
  target_compile_features(test_vulkan PRIVATE cxx_std_17)
  target_compile_options(test_vulkan PRIVATE -Wall -Wextra -Werror -Wno-missing-field-initializers)
  add_test(NAME vulkan COMMAND test_vulkan)
  set_tests_properties(vulkan PROPERTIES SKIP_RETURN_CODE 77)
endif()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  foreach(name recon functions harness lifted)
    add_test(NAME ${name} COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_${name}.py)
  endforeach()
  set_tests_properties(harness lifted PROPERTIES SKIP_REGULAR_EXPRESSION "OK \\(skipped=")
endif()
