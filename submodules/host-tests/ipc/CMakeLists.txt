# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The IPC4 transport carries requests to a DSP model and their replies back: the loader's register order, a budget per message class, status and crash replies, notifications, the interrupt mode and its storm guard, two callers, large configurations in blocks and the poll policy; and the DSP driver's stream manager runs its transitions over a fake hdaudbus and the model with the loopback topology, over the shared case rows, the self-test's period wait being the driver's own and answered by a fake clock and fake positions, under ASan and UBSan
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_library(ipc_common INTERFACE)
target_include_directories(ipc_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../fwload/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/../fwload
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/sofipc4
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/sofipc
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/softplg
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/intcdsp
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/hdaudbus
    ${REACTOS_HOST_SOURCE_ROOT}/modules/rostests/kmtests/fwload
    ${REACTOS_HOST_SOURCE_ROOT}/modules/rostests/kmtests/intcdsp)

# the stream cases wrap the transport's request to fail one exchange with the link ready (N7-02): sofipc.c's own entry is compiled under another name for the wrapper
set_source_files_properties(${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/sofipc/sofipc.c PROPERTIES
    COMPILE_DEFINITIONS "SofIpcRequest=SofIpcRequestReal")

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
    ${REACTOS_HOST_SOURCE_ROOT}/submodules/host-tests/fwload/fake_bus.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload/sof_ri.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload/cl_dma.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload/fwboot.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload/fwimage.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/sofipc4/sofipc4.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/sofipc/sofipc.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/softplg/softplg.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/softplg/ipc4model.c
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/intcdsp/adspbus.c
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/intcdsp/stream.c)
# the stream cases read the loopback topology (the parser tests' fixture) and resolve its UUIDs against the real firmware image (the loader tests' fixture)
target_compile_definitions(sofipc PRIVATE
    FWLOAD_FIXTURE_DIR="${REACTOS_HOST_SOURCE_ROOT}/submodules/host-tests/fwload/fixtures"
    SOFTPLG_FIXTURE_DIR="${REACTOS_HOST_SOURCE_ROOT}/submodules/host-tests/dspparsers/fixtures")
