# ApoLite - the Windows 11 APO shell (ApoLite.dll).

add_library(ApoLite SHARED
    apo.cpp
    apo_registration.cpp
    apo_iids.cpp
    class_factory.cpp
    dllmain.cpp
    apolite.def
)

target_include_directories(ApoLite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ApoLite PRIVATE apolite_core ole32 oleaut32 uuid advapi32)

if(MSVC)
    target_compile_options(ApoLite PRIVATE /W4 /permissive- /fp:fast /EHsc)
    target_compile_definitions(ApoLite PRIVATE _USE_MATH_DEFINES NOMINMAX UNICODE _UNICODE)
    # An APO is loaded into the audio engine process; it must not require a
    # separately installed Visual C++ runtime.
    set_property(TARGET ApoLite PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
else()
    target_compile_options(ApoLite PRIVATE -Wall -Wextra -O3)
    target_compile_definitions(ApoLite PRIVATE UNICODE _UNICODE)
    # Static runtime keeps the DLL free of external toolchain dependencies.
    target_link_options(ApoLite PRIVATE -static -static-libgcc -static-libstdc++
                                       -Wl,--kill-at -s)
endif()

install(TARGETS ApoLite RUNTIME DESTINATION .)
