
include_directories(
    ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include
    ${REACTOS_SOURCE_DIR}/base/system/dwm)

list(APPEND SOURCE
    Material.c
    ${REACTOS_SOURCE_DIR}/base/system/dwm/libermaterial.c
    testlist.c)

add_executable(dwmmaterial_unittest ${SOURCE})
set_module_type(dwmmaterial_unittest win32cui)
add_importlibs(dwmmaterial_unittest advapi32 user32 gdi32 msvcrt kernel32 ntdll)
add_dependencies(dwmmaterial_unittest liber_tokens)
add_rostests_file(TARGET dwmmaterial_unittest)
