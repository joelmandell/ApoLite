cmake_minimum_required(VERSION 3.20)

project(ApoLite VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
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
option(APOLITE_BUILD_TESTS "Build the test suite" ON)

if(APOLITE_BUILD_CLI)
    add_executable(apolite_cli src/cli/main.cpp)
    target_link_libraries(apolite_cli PRIVATE apolite_core)
    set_target_properties(apolite_cli PROPERTIES OUTPUT_NAME apolite)
endif()

if(WIN32 AND APOLITE_BUILD_APO)
    add_subdirectory(src/win)
endif()

if(APOLITE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS apolite_core ARCHIVE DESTINATION lib)
