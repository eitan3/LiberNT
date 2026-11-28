
spec2def(perfos.dll perfos.spec)

add_library(perfos MODULE
    perfos.c
    ${CMAKE_CURRENT_BINARY_DIR}/perfos.def)

set_module_type(perfos win32dll)
add_importlibs(perfos msvcrt kernel32 ntdll)
add_cd_file(TARGET perfos DESTINATION reactos/system32 FOR all)

add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/perfc009.dat DESTINATION reactos/system32 FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/perfh009.dat DESTINATION reactos/system32 FOR all)
