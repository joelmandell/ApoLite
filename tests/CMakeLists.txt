# ApoLite - test suite.

add_executable(test_apolite test_apolite.cpp)
target_link_libraries(test_apolite PRIVATE apolite_core)

if(MSVC)
    target_compile_options(test_apolite PRIVATE /W4 /permissive- /EHsc)
    target_compile_definitions(test_apolite PRIVATE _USE_MATH_DEFINES NOMINMAX)
else()
    target_compile_options(test_apolite PRIVATE -Wall -Wextra)
endif()

add_test(NAME apolite_unit COMMAND test_apolite)
