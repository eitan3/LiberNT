# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The apitests' identity of the DSP's PCM in the device tree (modules/rostests/apitests/include/apitest_dspidentity.h, the header they compile): the audio child's instance path and the KS interface paths of its subdevices are the PCM's, another bus's and another device's are not; under ASan and UBSan
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_executable(dspidentity t_dspidentity.c)

# the stubs come first: the header takes apitest.h from there
target_include_directories(dspidentity PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${REACTOS_HOST_SOURCE_ROOT}/modules/rostests/apitests/include)

target_compile_features(dspidentity PRIVATE c_std_99)
target_compile_options(dspidentity PRIVATE -Wall -Werror -Wno-unused-function -g
    -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(dspidentity PRIVATE -fsanitize=address,undefined)

add_test(NAME dspidentity.identity COMMAND dspidentity)
