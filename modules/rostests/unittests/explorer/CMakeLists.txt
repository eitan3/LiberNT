
include_directories(
    ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include
    ${REACTOS_SOURCE_DIR}/base/shell/explorer)

list(APPEND SOURCE
    Calendar.cpp
    Power.cpp
    ${REACTOS_SOURCE_DIR}/base/shell/explorer/calstore.cpp
    ${REACTOS_SOURCE_DIR}/base/shell/explorer/powermode.cpp
    testlist.c)

add_executable(explorer_unittest ${SOURCE})
set_module_type(explorer_unittest win32cui)
target_compile_definitions(explorer_unittest PRIVATE UNICODE _UNICODE)
add_importlibs(explorer_unittest advapi32 user32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET explorer_unittest)
