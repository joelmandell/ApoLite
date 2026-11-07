cmake_minimum_required(VERSION 3.20)

project(ApoLite VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(WIN32)
    # ApoLite targets Windows 11 only, so the newest API surface is available
    # (per-monitor DPI awareness, for instance) without runtime probing.
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

if(MSVC)
    # Static CRT for every target. The APO DLL is loaded into the audio engine
    # process and must not need a separately installed Visual C++ runtime, and
    # mixing /MT with /MD fails at link time.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    # ApoLite uses the portable C stdio/string functions deliberately.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# The version lives in include/apolite/apolite.h. Parse it here so the CMake
# project version and the Windows VERSIONINFO resource are derived from that one
# copy instead of being kept in sync by hand. This runs on every platform, so a
# mismatch fails fast even on a Linux build that never touches the resources.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/apolite/apolite.h" _apolite_version
     REGEX "^#define APOLITE_VERSION_")
foreach(_line IN LISTS _apolite_version)
    if(_line MATCHES "APOLITE_VERSION_MAJOR[ \t]+([0-9]+)")
        set(APOLITE_VERSION_MAJOR "${CMAKE_MATCH_1}")
    elseif(_line MATCHES "APOLITE_VERSION_MINOR[ \t]+([0-9]+)")
        set(APOLITE_VERSION_MINOR "${CMAKE_MATCH_1}")
    elseif(_line MATCHES "APOLITE_VERSION_PATCH[ \t]+([0-9]+)")
        set(APOLITE_VERSION_PATCH "${CMAKE_MATCH_1}")
    elseif(_line MATCHES "APOLITE_VERSION_STRING[ \t]+\"([^\"]+)\"")
        set(APOLITE_VERSION_STRING "${CMAKE_MATCH_1}")
    endif()
endforeach()

# Check for an empty value rather than falsiness: a version component of 0 is
# valid (1.0.0), and `NOT 0` would wrongly reject it.
if("${APOLITE_VERSION_MAJOR}" STREQUAL "" OR "${APOLITE_VERSION_MINOR}" STREQUAL ""
   OR "${APOLITE_VERSION_PATCH}" STREQUAL "" OR "${APOLITE_VERSION_STRING}" STREQUAL "")
    message(FATAL_ERROR
        "Could not parse APOLITE_VERSION_* from include/apolite/apolite.h.")
endif()

if(NOT "${PROJECT_VERSION}" STREQUAL "${APOLITE_VERSION_STRING}")
    message(FATAL_ERROR
        "project() version ${PROJECT_VERSION} does not match APOLITE_VERSION_STRING "
        "${APOLITE_VERSION_STRING} in include/apolite/apolite.h. Bump both together.")
endif()

if(WIN32)
    enable_language(RC)

    # Adds a VERSIONINFO resource so the patch digit is visible in the Windows
    # file properties dialog; APO_REG_PROPERTIES has no field for it.
    function(apolite_add_version_resource target filetype description internal_name original_name)
        set(APOLITE_FILETYPE "${filetype}")
        set(APOLITE_FILEDESCRIPTION "${description}")
        set(APOLITE_INTERNALNAME "${internal_name}")
        set(APOLITE_ORIGINALFILENAME "${original_name}")
        set(_generated "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
        configure_file("${CMAKE_SOURCE_DIR}/resources/apolite.rc.in" "${_generated}" @ONLY)
        target_sources(${target} PRIVATE "${_generated}")
    endfunction()
endif()

# The portable DSP core builds on any platform; only the APO shell is Windows.
add_library(apolite_core STATIC
    src/core/fft.cpp
    src/core/biquad.cpp
    src/core/gain.cpp
    src/core/fir.cpp
    src/core/expression.cpp
    src/core/channel.cpp
    src/core/filters.cpp
    src/core/config.cpp
    src/core/engine.cpp
    src/core/impulse.cpp
    src/core/settings.cpp
    src/core/registry.cpp
)
target_include_directories(apolite_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(apolite_core PRIVATE /W4 /permissive- /fp:fast)
    target_compile_definitions(apolite_core PRIVATE _USE_MATH_DEFINES NOMINMAX)
else()
    target_compile_options(apolite_core PRIVATE -Wall -Wextra -O3)
endif()

option(APOLITE_BUILD_APO "Build the Windows APO DLL" ON)
option(APOLITE_BUILD_CLI "Build the apolite command-line tool" ON)
option(APOLITE_BUILD_GUI "Build the ApoLite editor (Windows only)" ON)
option(APOLITE_BUILD_SETUP "Build the ApoLite device setup tool (Windows only)" ON)
option(APOLITE_BUILD_TESTS "Build the test suite" ON)
option(APOLITE_BUILD_BENCH "Build the processing-cost benchmark" ON)

if(APOLITE_BUILD_CLI)
    add_executable(apolite_cli src/cli/main.cpp)
    target_link_libraries(apolite_cli PRIVATE apolite_core)
    set_target_properties(apolite_cli PROPERTIES OUTPUT_NAME apolite)
endif()

if(APOLITE_BUILD_BENCH)
    add_executable(apolite_bench tools/bench.cpp)
    target_link_libraries(apolite_bench PRIVATE apolite_core)
    # The benchmark reads config/voice-mic.txt, so run it from the source root.
    set_target_properties(apolite_bench PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(WIN32 AND APOLITE_BUILD_APO)
    add_subdirectory(src/win)
endif()

if(WIN32 AND APOLITE_BUILD_GUI)
    add_subdirectory(src/gui)
endif()

if(WIN32 AND APOLITE_BUILD_SETUP)
    add_subdirectory(src/setup)
endif()

if(APOLITE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS apolite_core ARCHIVE DESTINATION lib)
