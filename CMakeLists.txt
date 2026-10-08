cmake_minimum_required(VERSION 3.20)
project(PKZLib LANGUAGES C CXX)

# PKZLib is header-only; this file builds zlib for it, a tool and a self-test.
option(PKZLIB_BUILD_TOOLS "Build the pkztool command-line utility" ON)
option(PKZLIB_BUILD_TESTS "Build the header compile / round-trip self-test" ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/contrib/zlib/CMakeLists.txt")
    message(FATAL_ERROR "contrib/zlib is empty: run `git submodule update --init` in PKZLib")
endif()

set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_MINIZIP OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(ZLIB_PREFIX OFF CACHE BOOL "" FORCE)
add_subdirectory(contrib/zlib EXCLUDE_FROM_ALL)

# Texture code on the thirdparty/textures submodules (ReXGlue, libsquish, stb), kept private to this library.
set(PKZLIB_TEXTURES "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/textures")
if(NOT EXISTS "${PKZLIB_TEXTURES}/rexglue/CMakeLists.txt")
    message(FATAL_ERROR "thirdparty/textures is empty: run `git submodule update --init` in PKZLib")
endif()
add_subdirectory("${PKZLIB_TEXTURES}/rexglue" EXCLUDE_FROM_ALL)

set(PKZLIB_SQUISH "${PKZLIB_TEXTURES}/libsquish/lib/src")
add_library(pkzlib_textures STATIC
    Graphics/XenosTexture.cpp
    BaseUtils/BUPng.cpp
    "${PKZLIB_SQUISH}/alpha.cpp"
    "${PKZLIB_SQUISH}/clusterfit.cpp"
    "${PKZLIB_SQUISH}/colourblock.cpp"
    "${PKZLIB_SQUISH}/colourfit.cpp"
    "${PKZLIB_SQUISH}/colourset.cpp"
    "${PKZLIB_SQUISH}/maths.cpp"
    "${PKZLIB_SQUISH}/rangefit.cpp"
    "${PKZLIB_SQUISH}/singlecolourfit.cpp"
    "${PKZLIB_SQUISH}/squish.cpp")
target_include_directories(pkzlib_textures PRIVATE
    "${PKZLIB_TEXTURES}/libsquish/lib/include"
    "${PKZLIB_TEXTURES}/squish_export"
    "${PKZLIB_TEXTURES}/stb")
target_link_libraries(pkzlib_textures PRIVATE rexglue_textures)

add_library(pkzlib INTERFACE)
target_include_directories(pkzlib INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/contrib/zlib"
    "${CMAKE_CURRENT_BINARY_DIR}/contrib/zlib")   # zconf.h is generated here
target_link_libraries(pkzlib INTERFACE zlibstatic pkzlib_textures)
target_compile_features(pkzlib INTERFACE cxx_std_17)
if(WIN32)
    target_compile_definitions(pkzlib INTERFACE _CRT_SECURE_NO_WARNINGS)
endif()

if(PKZLIB_BUILD_TOOLS)
    add_executable(pkztool tools/pkztool.cpp)
    target_link_libraries(pkztool PRIVATE pkzlib)
endif()

if(PKZLIB_BUILD_TESTS)
    enable_testing()
    add_executable(pkzlib_selftest tests/selftest.cpp)
    target_link_libraries(pkzlib_selftest PRIVATE pkzlib)
    add_test(NAME pkzlib_selftest COMMAND pkzlib_selftest)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME pkzfields_selftest
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pkzfields.py")
    endif()
endif()
