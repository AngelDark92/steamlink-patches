cmake_minimum_required(VERSION 3.22)
project(gxr_hybrid_codec LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(gxr_hybrid_codec SHARED
    src/gxh1_parse.cpp
    src/astc_decode.cpp
    src/pyrowave_decode.cpp
    src/hybrid_session.cpp
    src/hybrid_entry.cpp)
target_include_directories(gxr_hybrid_codec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/protocol")
target_compile_options(gxr_hybrid_codec PRIVATE -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers -ffunction-sections -fdata-sections)
target_link_options(gxr_hybrid_codec PRIVATE -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,--exclude-libs,ALL -static-libstdc++)
set_target_properties(gxr_hybrid_codec PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
