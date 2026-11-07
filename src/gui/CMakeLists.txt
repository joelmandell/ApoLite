# ApoLite - the editor. A plain Win32 window, so it needs no UI framework.
add_executable(ApoLiteEditor WIN32
    main_gui.cpp
    curve.cpp
)

target_include_directories(ApoLiteEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ApoLiteEditor PRIVATE apolite_core comctl32 gdi32 user32 shell32 ole32)

if(MSVC)
    target_compile_options(ApoLiteEditor PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_definitions(ApoLiteEditor PRIVATE _USE_MATH_DEFINES NOMINMAX UNICODE _UNICODE)
else()
    target_compile_options(ApoLiteEditor PRIVATE -Wall -Wextra -O2)
    target_compile_definitions(ApoLiteEditor PRIVATE UNICODE _UNICODE)
    # WIN32 gives the mingw build a wWinMain entry point; the runtime stays
    # static so the editor is a single self-contained file.
    target_link_options(ApoLiteEditor PRIVATE -municode -static -static-libgcc
                                              -static-libstdc++ -s)
endif()

install(TARGETS ApoLiteEditor RUNTIME DESTINATION .)

apolite_add_version_resource(ApoLiteEditor VFT_APP
    "ApoLite editor" "ApoLiteEditor" "ApoLiteEditor.exe")
