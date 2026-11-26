
include_directories(${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include)

list(APPEND SOURCE
    Anim.cpp
    Color.cpp
    Controls.cpp
    Input.cpp
    Layout.cpp
    Painter.cpp
    Palette.cpp
    Tokens.cpp
    testlist.c)

add_executable(liberui_unittest ${SOURCE})
target_link_libraries(liberui_unittest liberui shellframe cpprt uuid)
set_module_type(liberui_unittest win32cui)
target_compile_definitions(liberui_unittest PRIVATE UNICODE _UNICODE)
add_importlibs(liberui_unittest gdiplus gdi32 user32 msvcrt kernel32 ntdll)
add_dependencies(liberui_unittest liber_tokens)
add_rostests_file(TARGET liberui_unittest)
