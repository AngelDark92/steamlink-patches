cmake_minimum_required(VERSION 3.22)
project(gxr_single_projection_reconstruction LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OPENXR_SDK_SOURCE_DIR "" CACHE PATH "Path to an OpenXR-SDK checkout")
set(OPENXR_LOADER_LIBRARY "" CACHE FILEPATH "Path to Steam Link's arm64 libopenxr_loader.so")
if(NOT OPENXR_SDK_SOURCE_DIR)
    set(_bundled_sdk "${CMAKE_CURRENT_SOURCE_DIR}/../../../GalaxyXR-APK/tools-galaxyxr-native/OpenXR-SDK-1.1.61")
    if(EXISTS "${_bundled_sdk}/include/openxr/openxr.h")
        set(OPENXR_SDK_SOURCE_DIR "${_bundled_sdk}")
    endif()
endif()
if(NOT EXISTS "${OPENXR_SDK_SOURCE_DIR}/include/openxr/openxr.h")
    message(FATAL_ERROR "Set OPENXR_SDK_SOURCE_DIR to the bundled OpenXR-SDK 1.1.61 checkout")
endif()

add_library(gxr_single_projection_reconstruction_efficient_v1 SHARED
    src/single_projection_reconstruction_efficient_layer.cpp
)
target_include_directories(gxr_single_projection_reconstruction_efficient_v1 PRIVATE
    "${OPENXR_SDK_SOURCE_DIR}/include"
)
target_compile_definitions(gxr_single_projection_reconstruction_efficient_v1 PRIVATE
    XR_USE_PLATFORM_ANDROID
    XR_USE_GRAPHICS_API_OPENGL_ES
    GXR_LAYER_NAME="XR_APILAYER_local_GalaxyXR_single_projection_reconstruction_efficient_v1"
)
target_link_libraries(gxr_single_projection_reconstruction_efficient_v1 PRIVATE log EGL GLESv3)
set_target_properties(gxr_single_projection_reconstruction_efficient_v1 PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    OUTPUT_NAME gxr_single_projection_reconstruction_efficient_v1
)

if(NOT OPENXR_LOADER_LIBRARY)
    set(_analysis_loader "${CMAKE_CURRENT_SOURCE_DIR}/build-native-hook-analysis/lib/arm64-v8a/libopenxr_loader.so")
    if(EXISTS "${_analysis_loader}")
        set(OPENXR_LOADER_LIBRARY "${_analysis_loader}")
    endif()
endif()
if(EXISTS "${OPENXR_LOADER_LIBRARY}")
    add_library(gxr_single_projection_native_renderer_v1 SHARED
        src/single_projection_reconstruction_efficient_layer.cpp
    )
    target_include_directories(gxr_single_projection_native_renderer_v1 PRIVATE
        "${OPENXR_SDK_SOURCE_DIR}/include"
    )
    target_compile_definitions(gxr_single_projection_native_renderer_v1 PRIVATE
        XR_USE_PLATFORM_ANDROID
        XR_USE_GRAPHICS_API_OPENGL_ES
        GXR_NATIVE_RENDERER_HELPER
        GXR_LAYER_NAME="libgxr_nsp"
    )
    target_link_options(gxr_single_projection_native_renderer_v1 PRIVATE "-Wl,--no-as-needed")
    target_link_libraries(gxr_single_projection_native_renderer_v1 PRIVATE
        log EGL GLESv3 dl "${OPENXR_LOADER_LIBRARY}"
    )
    set_target_properties(gxr_single_projection_native_renderer_v1 PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        OUTPUT_NAME gxr_nsp
    )
else()
    message(STATUS
        "Native 5002322 hook target disabled: set OPENXR_LOADER_LIBRARY to the exact arm64 libopenxr_loader.so"
    )
endif()

add_library(gxr_single_projection_fovea_quads_v1 SHARED src/single_projection_fovea_quads_layer.cpp)
target_include_directories(gxr_single_projection_fovea_quads_v1 PRIVATE
    "${OPENXR_SDK_SOURCE_DIR}/include"
)
target_compile_definitions(gxr_single_projection_fovea_quads_v1 PRIVATE
    XR_USE_PLATFORM_ANDROID
    GXR_LAYER_NAME="XR_APILAYER_local_GalaxyXR_single_projection_fovea_quads_v1"
)
target_link_libraries(gxr_single_projection_fovea_quads_v1 PRIVATE log)
set_target_properties(gxr_single_projection_fovea_quads_v1 PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    OUTPUT_NAME gxr_single_projection_fovea_quads_v1
)
