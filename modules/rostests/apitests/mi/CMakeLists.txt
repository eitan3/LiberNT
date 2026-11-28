
list(APPEND SOURCE
    Client.c
    testlist.c)

add_executable(mi_apitest ${SOURCE})
set_module_type(mi_apitest win32cui)
add_importlibs(mi_apitest msvcrt kernel32 ntdll)
add_rostests_file(TARGET mi_apitest)
