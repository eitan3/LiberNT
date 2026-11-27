
include_directories(
    ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include
    ${REACTOS_SOURCE_DIR}/base/shell/explorer)

list(APPEND SOURCE
    Audio.cpp
    Calendar.cpp
    Dock.cpp
    Power.cpp
    StartModel.cpp
    Tray.cpp
    Widgets.cpp
    ${REACTOS_SOURCE_DIR}/base/shell/explorer/audiodev.cpp
    ${REACTOS_SOURCE_DIR}/base/shell/explorer/calstore.cpp
    ${REACTOS_SOURCE_DIR}/base/shell/explorer/dockmodel.cpp
    ${REACTOS_SOURCE_DIR}/base/shell/explorer/experience.cpp
    ${REACTOS_SOURCE_DIR}/base/shell/explorer/libershell.cpp
    ${REACTOS_SOURCE_DIR}/base/shell/explorer/powermode.cpp
    ${REACTOS_SOURCE_DIR}/base/shell/explorer/startmodel.cpp
    ${REACTOS_SOURCE_DIR}/base/shell/explorer/widgetmodel.cpp
    testlist.c)

add_executable(explorer_unittest ${SOURCE})
set_module_type(explorer_unittest win32cui)
target_compile_definitions(explorer_unittest PRIVATE UNICODE _UNICODE)
target_link_libraries(explorer_unittest liberui shellframe uuid cpprt atl_classes)
add_dependencies(explorer_unittest liber_tokens)
add_importlibs(explorer_unittest gdiplus gdi32 powrprof shell32 shlwapi ole32 advapi32 user32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET explorer_unittest)
