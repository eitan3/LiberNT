# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The firmware loader boots a DSP model from the real sof-arl-s.ri and from malformed images under ASan and UBSan, with the DSP child's bus binding
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_library(fwload_common INTERFACE)
target_include_directories(fwload_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/sofipc4
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/intcdsp
    # the child's bus interface mirror carries the bus's policy.h types
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/hdaudbus
    ${REACTOS_HOST_SOURCE_ROOT}/modules/rostests/kmtests/fwload)

function(fwload_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE fwload_common)
    target_compile_features(${name} PRIVATE c_std_99)
    target_compile_options(${name} PRIVATE -Wall -Werror -Wno-unused-function -g
        -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${name} PRIVATE -fsanitize=address,undefined)
    add_test(NAME ${name}.boot COMMAND ${name})
    set_tests_properties(${name}.boot PROPERTIES TIMEOUT 120)
endfunction()

fwload_test(fwload t_fwload.c fake_dsp.c fake_bus.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload/sof_ri.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload/cl_dma.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload/fwboot.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/fwload/fwimage.c
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/audio/sofipc4/sofipc4.c
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/intcdsp/adspbus.c)
target_compile_definitions(fwload PRIVATE
    FWLOAD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    # the Meteor Lake image the driver ships, read where it ships: the test does not duplicate the blob
    FWLOAD_FIRMWARE_DIR="${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/intcdsp/firmware")
