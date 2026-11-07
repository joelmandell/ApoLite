# ApoLite - device setup tool. Makes the APO selectable in the Enhancements tab
# by writing the endpoint association that regsvr32 does not.
add_executable(ApoLiteSetup
    main.cpp
    device_list.cpp
    win_registry.cpp
)

target_include_directories(ApoLiteSetup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ApoLiteSetup PRIVATE apolite_core ole32 advapi32)

if(MSVC)
    target_compile_options(ApoLiteSetup PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_definitions(ApoLiteSetup PRIVATE _USE_MATH_DEFINES NOMINMAX UNICODE _UNICODE)
else()
    target_compile_options(ApoLiteSetup PRIVATE -Wall -Wextra -O2)
    target_compile_definitions(ApoLiteSetup PRIVATE UNICODE _UNICODE)
    target_link_options(ApoLiteSetup PRIVATE -municode -static -static-libgcc -static-libstdc++ -s)
endif()

install(TARGETS ApoLiteSetup RUNTIME DESTINATION .)

apolite_add_version_resource(ApoLiteSetup VFT_APP
    "ApoLite device setup" "ApoLiteSetup" "ApoLiteSetup.exe")
