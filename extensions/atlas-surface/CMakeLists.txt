cmake_minimum_required(VERSION 3.22)
project(gxr_atlas_surface LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Native helper build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OPENXR_SDK_SOURCE_DIR "" CACHE PATH "Path to an OpenXR-SDK checkout")
if(NOT OPENXR_SDK_SOURCE_DIR)
    set(_bundled_sdk
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../builds/steamlink-patches/extensions/controller-velocity-layer/build-android/_deps/openxr_headers-src")
    if(EXISTS "${_bundled_sdk}/include/openxr/openxr.h")
        set(OPENXR_SDK_SOURCE_DIR "${_bundled_sdk}")
    endif()
endif()
if(NOT EXISTS "${OPENXR_SDK_SOURCE_DIR}/include/openxr/openxr.h")
    message(FATAL_ERROR "Set OPENXR_SDK_SOURCE_DIR to an OpenXR SDK checkout")
endif()

function(add_gxr_layer target output mode_define)
    add_library(${target} SHARED src/layer_core.cpp)
    target_include_directories(${target} PRIVATE
        "${OPENXR_SDK_SOURCE_DIR}/include"
    )
    target_compile_definitions(${target} PRIVATE
        XR_USE_PLATFORM_ANDROID
        XR_USE_GRAPHICS_API_OPENGL_ES
        NDEBUG
        ${mode_define}
    )
    target_compile_options(${target} PRIVATE
        -O2
        -ffunction-sections
        -fdata-sections
    )
    target_link_options(${target} PRIVATE
        -Wl,--gc-sections
    )
    target_link_libraries(${target} PRIVATE
        EGL
        GLESv3
        android
        nativewindow
        log
    )
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        OUTPUT_NAME ${output}
    )
    if(CMAKE_STRIP)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_STRIP}" --strip-unneeded "$<TARGET_FILE:${target}>"
            COMMENT "Strip unneeded symbols from ${output}"
            VERBATIM
        )
    endif()
endfunction()

add_gxr_layer(gxr_swapchain_enlarge gxr_swapchain_enlarge GXR_LAYER_MODE_ENLARGE)
add_gxr_layer(gxr_atlas_surface gxr_atlas_surface GXR_LAYER_MODE_ATLAS)
