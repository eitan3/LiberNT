# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     NHLT parser and audio path decision of hdaudbus, run over the shared case tables
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_executable(dsppath
    t_dsppath.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/nhlt/nhlt_parse.c
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/hdaudbus/policy.c)
target_include_directories(dsppath PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/nhlt
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/hdaudbus
    ${REACTOS_HOST_SOURCE_ROOT}/modules/rostests/kmtests/hdaudbus)
target_compile_features(dsppath PRIVATE c_std_99)
target_compile_options(dsppath PRIVATE -Wall -Werror -Wno-unused-function -g
    -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(dsppath PRIVATE -fsanitize=address,undefined)
add_test(NAME dsppath.rules COMMAND dsppath)
set_tests_properties(dsppath.rules PROPERTIES TIMEOUT 20)
