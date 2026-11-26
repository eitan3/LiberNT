
include_directories(
    ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include
    ${REACTOS_SOURCE_DIR}/base/shell/explorer)

list(APPEND SOURCE
    Store.cpp
    ${REACTOS_SOURCE_DIR}/base/shell/explorer/calstore.cpp
    testlist.c)

add_executable(calstore_unittest ${SOURCE})
set_module_type(calstore_unittest win32cui)
target_compile_definitions(calstore_unittest PRIVATE UNICODE _UNICODE)
add_importlibs(calstore_unittest advapi32 user32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET calstore_unittest)
