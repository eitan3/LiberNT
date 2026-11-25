# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The IPC4 transport carries requests to a DSP model and their replies back: the loader's register order, a budget per message class, status and crash replies, notifications, the interrupt mode and its storm guard, two callers, large configurations in blocks and the poll policy, over the shared case rows, under ASan and UBSan
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_library(ipc_common INTERFACE)
target_include_directories(ipc_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../fwload/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/../fwload
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/sofipc4
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/sofipc
    ${REACTOS_HOST_SOURCE_ROOT}/modules/rostests/kmtests/fwload
    ${REACTOS_HOST_SOURCE_ROOT}/modules/rostests/kmtests/intcdsp)

function(ipc_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE ipc_common)
    target_compile_features(${name} PRIVATE c_std_99)
    target_compile_options(${name} PRIVATE -Wall -Werror -Wno-unused-function -g
        -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${name} PRIVATE -fsanitize=address,undefined)
    add_test(NAME ${name}.transport COMMAND ${name})
    set_tests_properties(${name}.transport PROPERTIES TIMEOUT 120)
endfunction()

ipc_test(sofipc t_sofipc.c ${REACTOS_HOST_SOURCE_ROOT}/submodules/host-tests/fwload/fake_dsp.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload/sof_ri.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload/cl_dma.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload/fwboot.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload/fwimage.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/sofipc4/sofipc4.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/sofipc/sofipc.c)
