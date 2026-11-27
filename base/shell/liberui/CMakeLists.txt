
list(APPEND SOURCE
    anim.cpp
    color.cpp
    controls.cpp
    d3dpainter.cpp
    gpusurface.cpp
    input.cpp
    layout.cpp
    painter.cpp
    palette.cpp
    surface.cpp
    tokens.cpp
    widget.cpp)

add_library(liberui STATIC ${SOURCE})
target_include_directories(liberui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(liberui PRIVATE UNICODE _UNICODE)
target_link_libraries(liberui PRIVATE libmsvcrt oldnames shellframe uuid dxguid)
add_dependencies(liberui psdk liber_tokens)
