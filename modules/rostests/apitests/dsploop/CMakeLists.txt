# PROJECT:     LiberNT API tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)

# The loop through the DSP's endpoint: mmdevapi as the mmdevapi apitests use it, and the comparison apart from it (compare.c), which the host tests compile too
add_executable(dsploop_apitest dsploop.c compare.c testlist.c)
target_compile_definitions(dsploop_apitest PRIVATE _FORCENAMELESSUNION _FORCENAMELESSSTRUCT "CONST_VTABLE=")
set_module_type(dsploop_apitest win32cui)
target_link_libraries(dsploop_apitest uuid wine)
add_importlibs(dsploop_apitest ole32 advapi32 msvcrt kernel32 ntdll)
add_dependencies(dsploop_apitest dxsdk)
add_rostests_file(TARGET dsploop_apitest)
