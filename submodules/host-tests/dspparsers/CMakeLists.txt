# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The Intel SST parser libraries run over the shared case tables
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_library(dspparsers_common INTERFACE)
target_include_directories(dspparsers_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/nhlt
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/sofipc4
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/disco
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/hdaudbus
    ${REACTOS_HOST_SOURCE_ROOT}/modules/rostests/kmtests/hdaudbus)

function(dspparsers_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE dspparsers_common)
    target_compile_features(${name} PRIVATE c_std_99)
    target_compile_options(${name} PRIVATE -Wall -Werror -Wno-unused-function -g
        -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${name} PRIVATE -fsanitize=address,undefined)
    add_test(NAME ${name}.abi COMMAND ${name})
    set_tests_properties(${name}.abi PROPERTIES TIMEOUT 60)
endfunction()

dspparsers_test(sofipc4 t_sofipc4.c ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/sofipc4/sofipc4.c)
dspparsers_test(disco t_disco.c ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/disco/disco.c)
