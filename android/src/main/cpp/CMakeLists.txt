# CMakeLists.txt for ROTTR-Recomp Android native build
# This is called from Android's externalNativeBuild

cmake_minimum_required(VERSION 3.22)

# Include the main project's CMakeLists
# The main CMakeLists.txt handles all the build logic
# This file just needs to set up the Android-specific targets

# Get the project root directory
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)

# Include the main CMakeLists.txt
include("${PROJECT_ROOT}/CMakeLists.txt")

# The main CMakeLists.txt already defines:
# - rottr_runtime (static library with all subsystems)
# - rottr_android (shared library with android_main entry point)
# Both are built for arm64-v8a when ANDROID is defined

# Nothing else needed here - the main build handles everything