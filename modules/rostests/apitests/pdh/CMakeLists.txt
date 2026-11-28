
list(APPEND SOURCE
    CounterSamples.c
    FormatFromRawValue.c
    ProcessorFrequency.c
    testlist.c)

add_executable(pdh_apitest ${SOURCE})
set_module_type(pdh_apitest win32cui)
add_importlibs(pdh_apitest pdh powrprof msvcrt kernel32 ntdll)
add_rostests_file(TARGET pdh_apitest)
