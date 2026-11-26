# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The DSP endpoint driver's half free of I/O: the data ranges and the format negotiation of a streaming pin over an endpoint's formats, the descriptor tables' invariants over the same header the kernel builds, and each wave pin method as one call into a fake audio interface; under ASan and UBSan
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_executable(dsppcm t_dsppcm.c
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/drivers/dsppcm/ksendpoint.c)

target_include_directories(dsppcm PRIVATE
    # the KS and PortCls data types' stand-in first, then the kernel headers' of the loader tests
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/../fwload/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/../fwload
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/intcdsp
    # the endpoint driver's own directory before the codec driver's: both have a tables.h
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/drivers/dsppcm
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/drivers/hdaudio
    # the engine's cyclic scheduling rule the position arithmetic is run over
    ${REACTOS_HOST_SOURCE_ROOT}/dll/win32/mmdevapi)

target_compile_features(dsppcm PRIVATE c_std_99)
target_compile_options(dsppcm PRIVATE -Wall -Werror -Wno-unused-function -g
    -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(dsppcm PRIVATE -fsanitize=address,undefined)

add_test(NAME dsppcm.endpoint COMMAND dsppcm)
set_tests_properties(dsppcm.endpoint PROPERTIES TIMEOUT 120)
