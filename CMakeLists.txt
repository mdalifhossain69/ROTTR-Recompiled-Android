cmake_minimum_required(VERSION 3.22)
project(rottr_recomp C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Portable part: guest state, Remill helpers, Win32 stubs and runtime probes.
add_library(rottr_core STATIC
    ${CMAKE_CURRENT_LIST_DIR}/runtime/src/guest.c
    ${CMAKE_CURRENT_LIST_DIR}/runtime/src/guest_mem.c
    ${CMAKE_CURRENT_LIST_DIR}/runtime/src/pe_loader.cpp
    ${CMAKE_CURRENT_LIST_DIR}/runtime/src/remill_intrinsics.cpp
    ${CMAKE_CURRENT_LIST_DIR}/runtime/src/win32_stubs.cpp
    ${CMAKE_CURRENT_LIST_DIR}/runtime/src/x64_runtime.cpp
    ${CMAKE_CURRENT_LIST_DIR}/runtime/src/x64_interpreter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/runtime/src/x64_bridge.cpp
    ${CMAKE_CURRENT_LIST_DIR}/runtime/src/win32_host.cpp
)
target_include_directories(rottr_core PUBLIC ${CMAKE_CURRENT_LIST_DIR}/runtime/include)
target_compile_options(rottr_core PRIVATE -Wall -Wextra -Werror)
set_target_properties(rottr_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(ANDROID)
    add_library(rottr_runtime STATIC
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/graphics/graphics.cpp
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/graphics/dxgi_vk_format.cpp
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/graphics/dxbc_shader.cpp
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/graphics/d3d11_device.cpp
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/graphics/vulkan_backend.cpp
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/audio/audio.cpp
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/input/input.cpp
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/input/touch_controls.cpp
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/video/video.cpp
        ${CMAKE_CURRENT_LIST_DIR}/runtime/src/performance.cpp
    )
    target_include_directories(rottr_runtime PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/runtime/include ${CMAKE_CURRENT_LIST_DIR}/runtime/src)
    target_link_libraries(rottr_runtime PUBLIC rottr_core)

    # vkd3d-shader is the DXBC -> SPIR-V compiler. Keep the build usable for
    # developers without the optional library, but never fake successful translation.
    find_path(VKD3D_SHADER_INCLUDE_DIR vkd3d_shader.h)
    find_library(VKD3D_SHADER_LIBRARY NAMES vkd3d-shader vkd3d_shader)
    if(VKD3D_SHADER_INCLUDE_DIR AND VKD3D_SHADER_LIBRARY)
        target_include_directories(rottr_runtime PRIVATE ${VKD3D_SHADER_INCLUDE_DIR})
        target_link_libraries(rottr_runtime PRIVATE ${VKD3D_SHADER_LIBRARY})
        target_compile_definitions(rottr_runtime PRIVATE ROTTR_HAS_VKD3D_SHADER=1)
    endif()
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

add_executable(test_guest_mem tests/test_guest_mem.c)
target_link_libraries(test_guest_mem PRIVATE rottr_core)
target_compile_options(test_guest_mem PRIVATE -Wall -Wextra -Werror)
add_test(NAME guest_mem COMMAND test_guest_mem)

add_executable(test_pe_inspect tests/test_pe_inspect.cpp runtime/src/pe_inspect.cpp)
target_include_directories(test_pe_inspect PRIVATE runtime/include)
target_compile_features(test_pe_inspect PRIVATE cxx_std_17)
target_compile_options(test_pe_inspect PRIVATE -Wall -Wextra -Werror)
add_test(NAME pe_inspect COMMAND test_pe_inspect)

add_executable(test_pe_loader tests/test_pe_loader.cpp)
target_link_libraries(test_pe_loader PRIVATE rottr_core)
target_compile_options(test_pe_loader PRIVATE -Wall -Wextra -Werror)
add_test(NAME pe_loader COMMAND test_pe_loader)

add_executable(test_runtime tests/test_runtime.cpp)
target_link_libraries(test_runtime PRIVATE rottr_core)
target_compile_options(test_runtime PRIVATE -Wall -Wextra -Werror)
add_test(NAME runtime COMMAND test_runtime)

add_executable(test_x64_interpreter tests/test_x64_interpreter.cpp)
target_link_libraries(test_x64_interpreter PRIVATE rottr_core)
target_compile_options(test_x64_interpreter PRIVATE -Wall -Wextra -Werror)
add_test(NAME x64_interpreter COMMAND test_x64_interpreter)

add_executable(test_x64_bridge tests/test_x64_bridge.cpp)
target_link_libraries(test_x64_bridge PRIVATE rottr_core)
target_compile_options(test_x64_bridge PRIVATE -Wall -Wextra -Werror)
add_test(NAME x64_bridge COMMAND test_x64_bridge)

add_executable(test_x64_atomics tests/test_x64_atomics.cpp)
target_link_libraries(test_x64_atomics PRIVATE rottr_core)
target_compile_options(test_x64_atomics PRIVATE -Wall -Wextra -Werror -pthread)
add_test(NAME x64_atomics COMMAND test_x64_atomics)

add_executable(test_x64_cpuid tests/test_x64_cpuid.cpp)
target_link_libraries(test_x64_cpuid PRIVATE rottr_core)
target_compile_options(test_x64_cpuid PRIVATE -Wall -Wextra -Werror)
add_test(NAME x64_cpuid COMMAND test_x64_cpuid)

add_executable(test_win32_exit tests/test_win32_exit.cpp)
target_link_libraries(test_win32_exit PRIVATE rottr_core)
target_compile_options(test_win32_exit PRIVATE -Wall -Wextra -Werror)
add_test(NAME win32_exit COMMAND test_win32_exit)

add_executable(test_pe_execute tests/test_pe_execute.cpp)
target_link_libraries(test_pe_execute PRIVATE rottr_core)
target_compile_options(test_pe_execute PRIVATE -Wall -Wextra -Werror)
add_test(NAME pe_execute COMMAND test_pe_execute)

add_executable(test_win32_handles tests/test_win32_handles.cpp)
target_link_libraries(test_win32_handles PRIVATE rottr_core)
target_compile_options(test_win32_handles PRIVATE -Wall -Wextra -Werror)
add_test(NAME win32_handles COMMAND test_win32_handles)

add_executable(test_win32_heap tests/test_win32_heap.cpp)
target_link_libraries(test_win32_heap PRIVATE rottr_core)
target_compile_options(test_win32_heap PRIVATE -Wall -Wextra -Werror)
add_test(NAME win32_heap COMMAND test_win32_heap)

add_executable(test_win32_files tests/test_win32_files.cpp)
target_link_libraries(test_win32_files PRIVATE rottr_core)
target_compile_options(test_win32_files PRIVATE -Wall -Wextra -Werror)
add_test(NAME win32_files COMMAND test_win32_files)

add_executable(test_win32_mmap tests/test_win32_mmap.cpp)
target_link_libraries(test_win32_mmap PRIVATE rottr_core)
target_compile_options(test_win32_mmap PRIVATE -Wall -Wextra -Werror)
add_test(NAME win32_mmap COMMAND test_win32_mmap)

add_executable(test_win32_sync tests/test_win32_sync.cpp)
target_link_libraries(test_win32_sync PRIVATE rottr_core)
target_compile_options(test_win32_sync PRIVATE -Wall -Wextra -Werror -pthread)
add_test(NAME win32_sync COMMAND test_win32_sync)

add_executable(test_win32_threads tests/test_win32_threads.cpp)
target_link_libraries(test_win32_threads PRIVATE rottr_core)
target_compile_options(test_win32_threads PRIVATE -Wall -Wextra -Werror -pthread)
add_test(NAME win32_threads COMMAND test_win32_threads)

add_executable(test_pe_modules tests/test_pe_modules.cpp)
target_link_libraries(test_pe_modules PRIVATE rottr_core)
target_compile_options(test_pe_modules PRIVATE -Wall -Wextra -Werror)
add_test(NAME pe_modules COMMAND test_pe_modules)

add_executable(test_win32_gs tests/test_win32_gs.cpp)
target_link_libraries(test_win32_gs PRIVATE rottr_core)
target_compile_options(test_win32_gs PRIVATE -Wall -Wextra -Werror)
add_test(NAME win32_gs COMMAND test_win32_gs)

add_executable(test_win32_tls tests/test_win32_tls.cpp)
target_link_libraries(test_win32_tls PRIVATE rottr_core)
target_compile_options(test_win32_tls PRIVATE -Wall -Wextra -Werror)
add_test(NAME win32_tls COMMAND test_win32_tls)

add_executable(test_win32_dllmain tests/test_win32_dllmain.cpp)
target_link_libraries(test_win32_dllmain PRIVATE rottr_core)
target_compile_options(test_win32_dllmain PRIVATE -Wall -Wextra -Werror)
add_test(NAME win32_dllmain COMMAND test_win32_dllmain)

add_executable(test_x64_runtime tests/test_x64_runtime.cpp)
target_link_libraries(test_x64_runtime PRIVATE rottr_core)
target_compile_options(test_x64_runtime PRIVATE -Wall -Wextra -Werror)
add_test(NAME x64_runtime COMMAND test_x64_runtime)

add_executable(test_dxgi_vk_format tests/test_dxgi_vk_format.cpp runtime/src/graphics/dxgi_vk_format.cpp)
target_include_directories(test_dxgi_vk_format PRIVATE runtime/src)
target_compile_options(test_dxgi_vk_format PRIVATE -Wall -Wextra -Werror)
add_test(NAME dxgi_vk_format COMMAND test_dxgi_vk_format)

add_executable(test_dxbc_shader tests/test_dxbc_shader.cpp runtime/src/graphics/dxbc_shader.cpp)
target_include_directories(test_dxbc_shader PRIVATE runtime/src)
target_compile_options(test_dxbc_shader PRIVATE -Wall -Wextra -Werror)
add_test(NAME dxbc_shader COMMAND test_dxbc_shader)

add_executable(test_d3d11_device tests/test_d3d11_device.cpp runtime/src/graphics/d3d11_device.cpp runtime/src/graphics/dxgi_vk_format.cpp)
target_include_directories(test_d3d11_device PRIVATE runtime/src)
target_compile_options(test_d3d11_device PRIVATE -Wall -Wextra -Werror)
add_test(NAME d3d11_device COMMAND test_d3d11_device)

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
