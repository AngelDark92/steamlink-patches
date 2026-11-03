cmake_minimum_required(VERSION 3.22)
project(gxr_atlas_surface_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OPENXR_SDK_SOURCE_DIR "" CACHE PATH "Path to an OpenXR-SDK checkout")
if(NOT OPENXR_SDK_SOURCE_DIR)
    set(_bundled_sdk
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../builds/steamlink-patches/extensions/controller-velocity-layer/build-android/_deps/openxr_headers-src")
    if(EXISTS "${_bundled_sdk}/include/openxr/openxr.h")
        set(OPENXR_SDK_SOURCE_DIR "${_bundled_sdk}")
    endif()
endif()
if(NOT EXISTS "${OPENXR_SDK_SOURCE_DIR}/include/openxr/openxr.h")
    message(FATAL_ERROR "Set OPENXR_SDK_SOURCE_DIR to an OpenXR SDK checkout")
endif()

enable_testing()
set(GXR_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${OPENXR_SDK_SOURCE_DIR}/include"
)

add_executable(layer_policy_tests layer_policy_tests.cpp)
target_include_directories(layer_policy_tests PRIVATE ${GXR_INCLUDES})
add_test(NAME layer_policy_tests COMMAND layer_policy_tests)
set_tests_properties(layer_policy_tests PROPERTIES TIMEOUT 30)

add_executable(layer_frame_tests layer_frame_tests.cpp)
target_include_directories(layer_frame_tests PRIVATE ${GXR_INCLUDES})
add_test(NAME layer_frame_tests COMMAND layer_frame_tests)
set_tests_properties(layer_frame_tests PROPERTIES TIMEOUT 30)
